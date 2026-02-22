#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/sdk/version.hpp>

namespace
{

constexpr std::int64_t kRequestIdInitialize = 1;
constexpr std::int64_t kRequestIdSamplingCreate = 5001;
constexpr std::int64_t kRequestIdTasksResult = 5002;
constexpr std::int64_t kRequestIdElicitationCreate = 5003;
constexpr std::int64_t kTaskPollIntervalMilliseconds = 250;
constexpr std::int64_t kTaskTtlMilliseconds = 5000;
constexpr std::int64_t kSamplingMaxTokens = 64;

auto makeInitializeRequest(std::int64_t requestId = kRequestIdInitialize) -> mcp::jsonrpc::Request  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = "initialize";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  (*request.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["capabilities"]["sampling"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["capabilities"]["sampling"]["tools"] = true;
  (*request.params)["capabilities"]["elicitation"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["capabilities"]["elicitation"]["form"] = true;
  (*request.params)["capabilities"]["tasks"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["capabilities"]["tasks"]["samplingCreateMessage"] = true;
  (*request.params)["capabilities"]["tasks"]["elicitationCreate"] = true;
  (*request.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"]["name"] = "host-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";
  return request;
}

auto completeInitialization(mcp::Server &server) -> void  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  static_cast<void>(server.handleRequest(mcp::jsonrpc::RequestContext {}, makeInitializeRequest()).get());

  mcp::jsonrpc::Notification initialized;
  initialized.method = "notifications/initialized";
  server.handleNotification(mcp::jsonrpc::RequestContext {}, initialized);
}

auto makeSuccessResponse(const mcp::jsonrpc::RequestId &id, mcp::jsonrpc::JsonValue result) -> mcp::jsonrpc::SuccessResponse  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  mcp::jsonrpc::SuccessResponse response;
  response.id = id;
  response.result = std::move(result);
  return response;
}

}  // namespace

auto main() -> int
{
  try
  {
    mcp::ServerConfiguration configuration;
    configuration.serverInfo = mcp::Implementation("example-bidirectional-server", "1.0.0");

    const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));
    completeInitialization(*server);

    std::unordered_map<std::string, mcp::jsonrpc::JsonValue> taskResults;
    std::size_t nextTaskId = 1;

    server->setOutboundMessageSender(
      [&server, &taskResults, &nextTaskId](const mcp::jsonrpc::RequestContext &context, mcp::jsonrpc::Message message) -> void
      {
        if (!std::holds_alternative<mcp::jsonrpc::Request>(message))
        {
          return;
        }

        const mcp::jsonrpc::Request &request = std::get<mcp::jsonrpc::Request>(message);

        if (request.method == "sampling/createMessage")
        {
          if (request.params.has_value() && request.params->is_object() && request.params->contains("task") && request.params->at("task").is_object())
          {
            const std::string taskId = "sampling-task-" + std::to_string(nextTaskId++);

            mcp::jsonrpc::JsonValue deferredResult = mcp::jsonrpc::JsonValue::object();
            deferredResult["role"] = "assistant";
            deferredResult["model"] = "example-model";
            deferredResult["content"] = mcp::jsonrpc::JsonValue::object();
            deferredResult["content"]["type"] = "text";
            deferredResult["content"]["text"] = "Task-completed sampling response";
            deferredResult["_meta"] = mcp::jsonrpc::JsonValue::object();
            deferredResult["_meta"]["io.modelcontextprotocol/related-task"] = mcp::jsonrpc::JsonValue::object();
            deferredResult["_meta"]["io.modelcontextprotocol/related-task"]["taskId"] = taskId;
            taskResults[taskId] = deferredResult;

            mcp::jsonrpc::JsonValue createTaskResult = mcp::jsonrpc::JsonValue::object();
            createTaskResult["task"] = mcp::jsonrpc::JsonValue::object();
            createTaskResult["task"]["taskId"] = taskId;
            createTaskResult["task"]["status"] = "working";
            createTaskResult["task"]["createdAt"] = "2026-01-01T00:00:00Z";
            createTaskResult["task"]["lastUpdatedAt"] = "2026-01-01T00:00:00Z";
            createTaskResult["task"]["pollInterval"] = std::int64_t {kTaskPollIntervalMilliseconds};

            const auto response = mcp::jsonrpc::Response {makeSuccessResponse(request.id, std::move(createTaskResult))};
            static_cast<void>(server->handleResponse(context, response));
            return;
          }

          mcp::jsonrpc::JsonValue samplingResult = mcp::jsonrpc::JsonValue::object();
          samplingResult["role"] = "assistant";
          samplingResult["model"] = "example-model";
          samplingResult["content"] = mcp::jsonrpc::JsonValue::object();
          samplingResult["content"]["type"] = "text";
          samplingResult["content"]["text"] = "Immediate sampling response";

          const auto response = mcp::jsonrpc::Response {makeSuccessResponse(request.id, std::move(samplingResult))};
          static_cast<void>(server->handleResponse(context, response));
          return;
        }

        if (request.method == "tasks/result")
        {
          if (!request.params.has_value() || !request.params->is_object() || !request.params->contains("taskId") || !request.params->at("taskId").is_string())
          {
            mcp::jsonrpc::ErrorResponse error;
            error.id = request.id;
            error.error = mcp::jsonrpc::makeInvalidParamsError(std::nullopt, "tasks/result requires params.taskId");
            static_cast<void>(server->handleResponse(context, mcp::jsonrpc::Response {error}));
            return;
          }

          const std::string taskId = request.params->at("taskId").as<std::string>();
          const auto resultIt = taskResults.find(taskId);
          if (resultIt == taskResults.end())
          {
            mcp::jsonrpc::ErrorResponse error;
            error.id = request.id;
            error.error = mcp::jsonrpc::makeInvalidParamsError(std::nullopt, "unknown taskId");
            static_cast<void>(server->handleResponse(context, mcp::jsonrpc::Response {error}));
            return;
          }

          const auto response = mcp::jsonrpc::Response {makeSuccessResponse(request.id, resultIt->second)};
          static_cast<void>(server->handleResponse(context, response));
          return;
        }

        if (request.method == "elicitation/create")
        {
          mcp::jsonrpc::JsonValue elicitationResult = mcp::jsonrpc::JsonValue::object();
          elicitationResult["action"] = "accept";
          elicitationResult["content"] = mcp::jsonrpc::JsonValue::object();
          elicitationResult["content"]["approved"] = true;
          elicitationResult["content"]["reason"] = "approved by simulated host";

          const auto response = mcp::jsonrpc::Response {makeSuccessResponse(request.id, std::move(elicitationResult))};
          static_cast<void>(server->handleResponse(context, response));
        }
      });

    mcp::jsonrpc::RequestContext context;
    context.sessionId = "example-session";

    mcp::jsonrpc::Request samplingRequest;
    samplingRequest.id = std::int64_t {kRequestIdSamplingCreate};
    samplingRequest.method = "sampling/createMessage";
    samplingRequest.params = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
    mcp::jsonrpc::JsonValue userMessage = mcp::jsonrpc::JsonValue::object();
    userMessage["role"] = "user";
    userMessage["content"] = mcp::jsonrpc::JsonValue::object();
    userMessage["content"]["type"] = "text";
    userMessage["content"]["text"] = "Summarize this conversation";
    (*samplingRequest.params)["messages"].push_back(std::move(userMessage));
    (*samplingRequest.params)["maxTokens"] = kSamplingMaxTokens;
    (*samplingRequest.params)["task"] = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["task"]["ttl"] = std::int64_t {kTaskTtlMilliseconds};

    const mcp::jsonrpc::Response samplingCreateResponse = server->sendRequest(context, std::move(samplingRequest)).get();
    if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(samplingCreateResponse))
    {
      throw std::runtime_error("sampling/createMessage task-augmented request failed");
    }

    const std::string taskId = std::get<mcp::jsonrpc::SuccessResponse>(samplingCreateResponse).result["task"]["taskId"].as<std::string>();
    std::cout << "sampling/createMessage returned taskId: " << taskId << '\n';

    mcp::jsonrpc::Request taskResultRequest;
    taskResultRequest.id = std::int64_t {kRequestIdTasksResult};
    taskResultRequest.method = "tasks/result";
    taskResultRequest.params = mcp::jsonrpc::JsonValue::object();
    (*taskResultRequest.params)["taskId"] = taskId;

    const mcp::jsonrpc::Response taskResultResponse = server->sendRequest(context, std::move(taskResultRequest)).get();
    if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskResultResponse))
    {
      throw std::runtime_error("tasks/result failed");
    }

    const mcp::jsonrpc::JsonValue &samplingResult = std::get<mcp::jsonrpc::SuccessResponse>(taskResultResponse).result;
    std::cout << "tasks/result text: " << samplingResult["content"]["text"].as<std::string>() << '\n';

    mcp::jsonrpc::Request elicitationRequest;
    elicitationRequest.id = std::int64_t {kRequestIdElicitationCreate};
    elicitationRequest.method = "elicitation/create";
    elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["mode"] = "form";
    (*elicitationRequest.params)["message"] = "Approve deploy?";
    (*elicitationRequest.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["type"] = "object";
    (*elicitationRequest.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["properties"]["approved"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["properties"]["approved"]["type"] = "boolean";

    const mcp::jsonrpc::Response elicitationResponse = server->sendRequest(context, std::move(elicitationRequest)).get();
    if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(elicitationResponse))
    {
      throw std::runtime_error("elicitation/create failed");
    }

    const mcp::jsonrpc::JsonValue &elicitationResult = std::get<mcp::jsonrpc::SuccessResponse>(elicitationResponse).result;
    std::cout << "elicitation action: " << elicitationResult["action"].as<std::string>() << '\n';
    std::cout << "elicitation approved: " << (elicitationResult["content"]["approved"].as<bool>() ? "true" : "false") << '\n';

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "bidirectional_sampling_elicitation failed: " << error.what() << '\n';
    return 1;
  }
}
