#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/util/tasks.hpp>

namespace
{

class DeterministicLongRunningOperation
{
public:
  DeterministicLongRunningOperation(mcp::util::TaskReceiver &receiver, mcp::jsonrpc::RequestContext context, std::string taskId)
    : receiver_(receiver)
    , context_(std::move(context))
    , taskId_(std::move(taskId))
  {
  }

  auto moveToInputRequired() -> mcp::util::TaskRecordResult
  {
    return receiver_.updateTaskStatus(context_, taskId_, mcp::util::TaskStatus::kInputRequired, std::string("Awaiting requestor input"));
  }

  auto moveBackToWorking() -> mcp::util::TaskRecordResult
  {
    return receiver_.updateTaskStatus(context_, taskId_, mcp::util::TaskStatus::kWorking, std::string("Resuming execution"));
  }

  auto completeWithSuccess() -> mcp::util::TaskRecordResult
  {
    mcp::jsonrpc::SuccessResponse response;
    response.id = std::int64_t {0};
    response.result = mcp::jsonrpc::JsonValue::object();
    response.result["content"] = mcp::jsonrpc::JsonValue::array();
    response.result["isError"] = false;
    response.result["trace"] = std::string("deterministic");

    return receiver_.completeTaskWithResponse(context_, taskId_, mcp::jsonrpc::Response {std::move(response)});
  }

  auto runToCompletion() -> mcp::util::TaskRecordResult
  {
    const mcp::util::TaskRecordResult inputRequired = moveToInputRequired();
    if (inputRequired.error != mcp::util::TaskStoreError::kNone)
    {
      return inputRequired;
    }

    const mcp::util::TaskRecordResult working = moveBackToWorking();
    if (working.error != mcp::util::TaskStoreError::kNone)
    {
      return working;
    }

    return completeWithSuccess();
  }

private:
  mcp::util::TaskReceiver &receiver_;
  mcp::jsonrpc::RequestContext context_;
  std::string taskId_;
};

auto makeTaskRequest(std::int64_t requestId, std::string method, std::string taskId) -> mcp::jsonrpc::Request
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = std::move(method);
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["taskId"] = std::move(taskId);
  return request;
}

auto makeTasksListRequest(std::int64_t requestId, std::optional<std::string> cursor = std::nullopt) -> mcp::jsonrpc::Request
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = "tasks/list";
  request.params = mcp::jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    (*request.params)["cursor"] = *cursor;
  }

  return request;
}

auto requireInvalidParams(const mcp::jsonrpc::Response &response) -> void
{
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
  const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(response);
  REQUIRE(errorResponse.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
}

auto makeTaskAugmentation() -> mcp::util::TaskAugmentationRequest
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["task"] = mcp::jsonrpc::JsonValue::object();
  params["task"]["ttl"] = 60000;

  std::string parseError;
  const mcp::util::TaskAugmentationRequest augmentation = mcp::util::parseTaskAugmentation(params, &parseError);
  REQUIRE(parseError.empty());
  REQUIRE(augmentation.requested);
  return augmentation;
}

}  // namespace

TEST_CASE("Task augmentation returns CreateTaskResult contract payload", "[conformance][tasks]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(context, makeTaskAugmentation());

  REQUIRE_FALSE(createdTask.task.taskId.empty());
  REQUIRE(createdTask.task.status == mcp::util::TaskStatus::kWorking);
  REQUIRE(createdTask.task.ttl.has_value());

  const mcp::jsonrpc::JsonValue resultPayload = mcp::util::createTaskResultToJson(createdTask);
  REQUIRE(resultPayload.contains("task"));
  REQUIRE(resultPayload["task"]["taskId"].as<std::string>() == createdTask.task.taskId);
  REQUIRE(resultPayload["task"]["status"].as<std::string>() == "working");
  REQUIRE_FALSE(resultPayload.contains("content"));
}

TEST_CASE("Task lifecycle supports transition path and terminal immutability", "[conformance][tasks]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(context, makeTaskAugmentation());

  DeterministicLongRunningOperation operation(receiver, context, createdTask.task.taskId);

  REQUIRE(operation.moveToInputRequired().error == mcp::util::TaskStoreError::kNone);
  const mcp::jsonrpc::Response inputRequiredGet = receiver.handleTasksGetRequest(context, makeTaskRequest(20, "tasks/get", createdTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(inputRequiredGet));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(inputRequiredGet).result["status"].as<std::string>() == "input_required");

  REQUIRE(operation.runToCompletion().error == mcp::util::TaskStoreError::kNone);

  const mcp::util::TaskRecordResult immutable = receiver.updateTaskStatus(context, createdTask.task.taskId, mcp::util::TaskStatus::kWorking, std::nullopt);
  REQUIRE(immutable.error == mcp::util::TaskStoreError::kTerminalImmutable);
}

TEST_CASE("tasks/result blocks until deterministic operation reaches terminal", "[conformance][tasks]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(context, makeTaskAugmentation());
  DeterministicLongRunningOperation operation(receiver, context, createdTask.task.taskId);

  const mcp::jsonrpc::Request resultRequest = makeTaskRequest(31, "tasks/result", createdTask.task.taskId);
  auto resultFuture =
    std::async(std::launch::async, [&receiver, &context, resultRequest]() -> mcp::jsonrpc::Response { return receiver.handleTasksResultRequest(context, resultRequest); });

  REQUIRE(resultFuture.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
  REQUIRE(operation.runToCompletion().error == mcp::util::TaskStoreError::kNone);

  const mcp::jsonrpc::Response response = resultFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));
  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
  REQUIRE(success.result["isError"].as<bool>() == false);
  REQUIRE(success.result["trace"].as<std::string>() == "deterministic");
}

TEST_CASE("Unknown taskId and invalid cursor return -32602", "[conformance][tasks]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);
  mcp::jsonrpc::RequestContext context;

  requireInvalidParams(receiver.handleTasksGetRequest(context, makeTaskRequest(1, "tasks/get", "missing-task")));
  requireInvalidParams(receiver.handleTasksResultRequest(context, makeTaskRequest(2, "tasks/result", "missing-task")));
  requireInvalidParams(receiver.handleTasksCancelRequest(context, makeTaskRequest(3, "tasks/cancel", "missing-task")));
  requireInvalidParams(receiver.handleTasksListRequest(context, makeTasksListRequest(4, "bad-cursor")));
}

TEST_CASE("related-task metadata is only injected into tasks/result", "[conformance][tasks]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  const mcp::util::CreateTaskResult queryTask = receiver.createTask(context, makeTaskAugmentation());
  const mcp::util::CreateTaskResult cancelTask = receiver.createTask(context, makeTaskAugmentation());
  DeterministicLongRunningOperation operation(receiver, context, queryTask.task.taskId);

  const mcp::jsonrpc::Response getResponse = receiver.handleTasksGetRequest(context, makeTaskRequest(10, "tasks/get", queryTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(getResponse));
  REQUIRE_FALSE(std::get<mcp::jsonrpc::SuccessResponse>(getResponse).result.contains("_meta"));

  const mcp::jsonrpc::Response listResponse = receiver.handleTasksListRequest(context, makeTasksListRequest(11));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(listResponse));
  REQUIRE_FALSE(std::get<mcp::jsonrpc::SuccessResponse>(listResponse).result.contains("_meta"));

  const mcp::jsonrpc::Response cancelResponse = receiver.handleTasksCancelRequest(context, makeTaskRequest(12, "tasks/cancel", cancelTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(cancelResponse));
  REQUIRE_FALSE(std::get<mcp::jsonrpc::SuccessResponse>(cancelResponse).result.contains("_meta"));

  REQUIRE(operation.completeWithSuccess().error == mcp::util::TaskStoreError::kNone);

  const mcp::jsonrpc::Response resultResponse = receiver.handleTasksResultRequest(context, makeTaskRequest(13, "tasks/result", queryTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(resultResponse));
  const auto &resultSuccess = std::get<mcp::jsonrpc::SuccessResponse>(resultResponse);
  REQUIRE(resultSuccess.result.contains("_meta"));
  REQUIRE(resultSuccess.result["_meta"].contains(std::string(mcp::util::kRelatedTaskMetadataKey)));
  REQUIRE(resultSuccess.result["_meta"][std::string(mcp::util::kRelatedTaskMetadataKey)]["taskId"].as<std::string>() == queryTask.task.taskId);
}

TEST_CASE("tasks/cancel rejects cancellation for terminal tasks", "[conformance][tasks]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(context, makeTaskAugmentation());
  DeterministicLongRunningOperation operation(receiver, context, createdTask.task.taskId);

  REQUIRE(operation.completeWithSuccess().error == mcp::util::TaskStoreError::kNone);

  const mcp::jsonrpc::Response cancelResponse = receiver.handleTasksCancelRequest(context, makeTaskRequest(40, "tasks/cancel", createdTask.task.taskId));
  requireInvalidParams(cancelResponse);
}

TEST_CASE("Task operations are bound to auth context", "[conformance][tasks]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext authA;
  authA.authContext = "mock-auth-a";
  mcp::jsonrpc::RequestContext authB;
  authB.authContext = "mock-auth-b";

  const mcp::util::CreateTaskResult taskBoundToA = receiver.createTask(authA, makeTaskAugmentation());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(receiver.handleTasksGetRequest(authA, makeTaskRequest(50, "tasks/get", taskBoundToA.task.taskId))));
  requireInvalidParams(receiver.handleTasksGetRequest(authB, makeTaskRequest(51, "tasks/get", taskBoundToA.task.taskId)));
}
