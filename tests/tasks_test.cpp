#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/util/tasks.hpp>

namespace
{

auto makeTaskRequest(std::string method, std::int64_t requestId, std::string taskId) -> mcp::jsonrpc::Request
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = std::move(method);
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["taskId"] = std::move(taskId);
  return request;
}

auto assertInvalidParams(const mcp::jsonrpc::Response &response) -> void
{
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
  const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
  REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
}

}  // namespace

TEST_CASE("InMemoryTaskStore enforces valid transitions and terminal immutability", "[tasks][store][transitions]")
{
  mcp::util::InMemoryTaskStore store;

  mcp::util::TaskCreateOptions createOptions;
  createOptions.pollInterval = 25;
  const mcp::util::TaskRecordResult created = store.createTask(std::move(createOptions));
  REQUIRE(created.error == mcp::util::TaskStoreError::kNone);
  REQUIRE(created.task.status == mcp::util::TaskStatus::kWorking);

  const mcp::util::TaskRecordResult inputRequired = store.updateTaskStatus(created.task.taskId, mcp::util::TaskStatus::kInputRequired, std::string("Need input"), std::nullopt);
  REQUIRE(inputRequired.error == mcp::util::TaskStoreError::kNone);
  REQUIRE(inputRequired.task.status == mcp::util::TaskStatus::kInputRequired);

  const mcp::util::TaskRecordResult backToWorking = store.updateTaskStatus(created.task.taskId, mcp::util::TaskStatus::kWorking, std::nullopt, std::nullopt);
  REQUIRE(backToWorking.error == mcp::util::TaskStoreError::kNone);
  REQUIRE(backToWorking.task.status == mcp::util::TaskStatus::kWorking);

  mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
  result["ok"] = true;
  const mcp::util::TaskRecordResult completed = store.setTaskResult(created.task.taskId, mcp::util::TaskStatus::kCompleted, std::nullopt, std::move(result), std::nullopt);
  REQUIRE(completed.error == mcp::util::TaskStoreError::kNone);
  REQUIRE(completed.task.status == mcp::util::TaskStatus::kCompleted);

  const mcp::util::TaskRecordResult immutable = store.updateTaskStatus(created.task.taskId, mcp::util::TaskStatus::kWorking, std::nullopt, std::nullopt);
  REQUIRE(immutable.error == mcp::util::TaskStoreError::kTerminalImmutable);

  mcp::JsonRpcError failure;
  failure.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError);
  failure.message = "late error";
  const mcp::util::TaskRecordResult lateError =
    store.setTaskError(created.task.taskId, mcp::util::TaskStatus::kFailed, std::string("should fail"), std::move(failure), std::nullopt);
  REQUIRE(lateError.error == mcp::util::TaskStoreError::kTerminalImmutable);
}

TEST_CASE("TaskReceiver cancellation rejects terminal tasks", "[tasks][cancel]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  context.authContext = "auth-a";

  mcp::util::TaskAugmentationRequest augmentation;
  augmentation.requested = true;
  const mcp::util::CreateTaskResult task = receiver.createTask(context, augmentation);

  mcp::jsonrpc::Request cancelRequest = makeTaskRequest("tasks/cancel", 1, task.task.taskId);
  const mcp::jsonrpc::Response firstCancel = receiver.handleTasksCancelRequest(context, cancelRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstCancel));

  const mcp::jsonrpc::Response secondCancel = receiver.handleTasksCancelRequest(context, cancelRequest);
  assertInvalidParams(secondCancel);
}

TEST_CASE("TaskReceiver tasks/result blocks until terminal and then unblocks", "[tasks][result][blocking]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  context.authContext = "auth-a";

  mcp::util::TaskAugmentationRequest augmentation;
  augmentation.requested = true;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(context, augmentation);

  mcp::jsonrpc::Request resultRequest = makeTaskRequest("tasks/result", 22, createdTask.task.taskId);
  auto resultFuture =
    std::async(std::launch::async, [&receiver, &context, resultRequest]() -> mcp::jsonrpc::Response { return receiver.handleTasksResultRequest(context, resultRequest); });

  REQUIRE(resultFuture.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);

  mcp::jsonrpc::SuccessResponse success;
  success.id = std::int64_t {0};
  success.result = mcp::jsonrpc::JsonValue::object();
  success.result["content"] = mcp::jsonrpc::JsonValue::array();
  success.result["isError"] = false;
  static_cast<void>(receiver.completeTaskWithResponse(context, createdTask.task.taskId, mcp::jsonrpc::Response {std::move(success)}));

  const mcp::jsonrpc::Response resultResponse = resultFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(resultResponse));
  const auto &successResult = std::get<mcp::jsonrpc::SuccessResponse>(resultResponse);
  REQUIRE(successResult.result.contains("_meta"));
  REQUIRE(successResult.result["_meta"].contains(std::string(mcp::util::kRelatedTaskMetadataKey)));
  REQUIRE(successResult.result["_meta"][std::string(mcp::util::kRelatedTaskMetadataKey)]["taskId"].as<std::string>() == createdTask.task.taskId);
}

TEST_CASE("TaskReceiver returns -32602 for invalid ids and cursors", "[tasks][validation]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;

  const mcp::jsonrpc::Response missingTaskGet = receiver.handleTasksGetRequest(context, makeTaskRequest("tasks/get", 1, "missing"));
  assertInvalidParams(missingTaskGet);

  const mcp::jsonrpc::Response missingTaskResult = receiver.handleTasksResultRequest(context, makeTaskRequest("tasks/result", 2, "missing"));
  assertInvalidParams(missingTaskResult);

  const mcp::jsonrpc::Response missingTaskCancel = receiver.handleTasksCancelRequest(context, makeTaskRequest("tasks/cancel", 3, "missing"));
  assertInvalidParams(missingTaskCancel);

  mcp::jsonrpc::Request listRequest;
  listRequest.id = std::int64_t {4};
  listRequest.method = "tasks/list";
  listRequest.params = mcp::jsonrpc::JsonValue::object();
  (*listRequest.params)["cursor"] = "not-a-valid-cursor";

  const mcp::jsonrpc::Response invalidCursor = receiver.handleTasksListRequest(context, listRequest);
  assertInvalidParams(invalidCursor);
}

TEST_CASE("TaskReceiver does not inject related-task metadata into tasks/get/list/cancel", "[tasks][metadata]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;
  context.authContext = "auth-a";

  mcp::util::TaskAugmentationRequest augmentation;
  augmentation.requested = true;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(context, augmentation);

  const mcp::jsonrpc::Response getResponse = receiver.handleTasksGetRequest(context, makeTaskRequest("tasks/get", 11, createdTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(getResponse));
  const auto &getSuccess = std::get<mcp::jsonrpc::SuccessResponse>(getResponse);
  REQUIRE_FALSE(getSuccess.result.contains("_meta"));

  const mcp::jsonrpc::Response cancelResponse = receiver.handleTasksCancelRequest(context, makeTaskRequest("tasks/cancel", 12, createdTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(cancelResponse));
  const auto &cancelSuccess = std::get<mcp::jsonrpc::SuccessResponse>(cancelResponse);
  REQUIRE_FALSE(cancelSuccess.result.contains("_meta"));

  mcp::jsonrpc::Request listRequest;
  listRequest.id = std::int64_t {13};
  listRequest.method = "tasks/list";
  listRequest.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response listResponse = receiver.handleTasksListRequest(context, listRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(listResponse));
  const auto &listSuccess = std::get<mcp::jsonrpc::SuccessResponse>(listResponse);
  REQUIRE(listSuccess.result.contains("tasks"));
  REQUIRE_FALSE(listSuccess.result.contains("_meta"));
}

TEST_CASE("TaskReceiver enforces auth-context binding", "[tasks][auth]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext authA;
  authA.authContext = "auth-a";
  mcp::jsonrpc::RequestContext authB;
  authB.authContext = "auth-b";

  mcp::util::TaskAugmentationRequest augmentation;
  augmentation.requested = true;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(authA, augmentation);

  const mcp::jsonrpc::Response allowedGet = receiver.handleTasksGetRequest(authA, makeTaskRequest("tasks/get", 20, createdTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(allowedGet));

  const mcp::jsonrpc::Response deniedGet = receiver.handleTasksGetRequest(authB, makeTaskRequest("tasks/get", 21, createdTask.task.taskId));
  assertInvalidParams(deniedGet);

  mcp::jsonrpc::Request listA;
  listA.id = std::int64_t {22};
  listA.method = "tasks/list";
  listA.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response listForA = receiver.handleTasksListRequest(authA, listA);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(listForA));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(listForA).result["tasks"].size() == 1);

  mcp::jsonrpc::Request listB = listA;
  listB.id = std::int64_t {23};
  const mcp::jsonrpc::Response listForB = receiver.handleTasksListRequest(authB, listB);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(listForB));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(listForB).result["tasks"].size() == 0);
}

TEST_CASE("TaskReceiver allows access to unbound tasks across auth contexts", "[tasks][auth]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext noAuth;
  mcp::jsonrpc::RequestContext authA;
  authA.authContext = "auth-a";
  mcp::jsonrpc::RequestContext authB;
  authB.authContext = "auth-b";

  mcp::util::TaskAugmentationRequest augmentation;
  augmentation.requested = true;
  const mcp::util::CreateTaskResult createdTask = receiver.createTask(noAuth, augmentation);

  const mcp::jsonrpc::Response getFromAuthA = receiver.handleTasksGetRequest(authA, makeTaskRequest("tasks/get", 24, createdTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(getFromAuthA));

  const mcp::jsonrpc::Response getFromAuthB = receiver.handleTasksGetRequest(authB, makeTaskRequest("tasks/get", 25, createdTask.task.taskId));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(getFromAuthB));
}

TEST_CASE("TaskReceiver supports ttl:null and finite ttl expiration", "[tasks][ttl]")
{
  auto store = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver receiver(store);

  mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::JsonValue nullTtlParams = mcp::jsonrpc::JsonValue::object();
  nullTtlParams["task"] = mcp::jsonrpc::JsonValue::object();
  nullTtlParams["task"]["ttl"] = mcp::jsonrpc::JsonValue::null();
  std::string parseError;
  const mcp::util::TaskAugmentationRequest nullTtlAugmentation = mcp::util::parseTaskAugmentation(nullTtlParams, &parseError);
  REQUIRE(parseError.empty());
  const mcp::util::CreateTaskResult nullTtlTask = receiver.createTask(context, nullTtlAugmentation);
  REQUIRE_FALSE(nullTtlTask.task.ttl.has_value());

  mcp::jsonrpc::JsonValue finiteTtlParams = mcp::jsonrpc::JsonValue::object();
  finiteTtlParams["task"] = mcp::jsonrpc::JsonValue::object();
  finiteTtlParams["task"]["ttl"] = 10;
  const mcp::util::TaskAugmentationRequest finiteTtlAugmentation = mcp::util::parseTaskAugmentation(finiteTtlParams, &parseError);
  REQUIRE(parseError.empty());
  const mcp::util::CreateTaskResult finiteTtlTask = receiver.createTask(context, finiteTtlAugmentation);
  REQUIRE(finiteTtlTask.task.ttl.has_value());
  REQUIRE(*finiteTtlTask.task.ttl == 10);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const mcp::jsonrpc::Response expiredGet = receiver.handleTasksGetRequest(context, makeTaskRequest("tasks/get", 30, finiteTtlTask.task.taskId));
  assertInvalidParams(expiredGet);
}
