#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/server/server.hpp>
#include <mcp/server/stdio_runner.hpp>

namespace
{

constexpr std::chrono::seconds kOutboundRequestTimeout {10};
constexpr std::chrono::seconds kSessionStateWaitTimeout {15};
constexpr std::int64_t kSamplingRequestId = 4101;
constexpr std::int64_t kElicitationRequestId = 4102;
constexpr std::string_view kExpectedSamplingResponseText = "reference-client-sampling-response";
constexpr std::string_view kExpectedElicitationReason = "reference-client-confirmed";

struct OutboundAssertionsState
{
  std::atomic<bool> started {false};
  std::atomic<bool> completed {false};
  std::atomic<bool> passed {false};
  std::mutex mutex;
  std::optional<std::string> failureReason;
  std::thread worker;
};

// Shared state between factory and fixture to track created server instances
struct ServerRegistry
{
  std::mutex mutex;
  std::vector<std::weak_ptr<mcp::server::Server>> servers;
};

auto makeTextContent(std::string text) -> mcp::jsonrpc::JsonValue
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = std::move(text);
  return content;
}

auto throwIfErrorResponse(const mcp::jsonrpc::Response &response, std::string_view methodName) -> void
{
  if (!std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response))
  {
    return;
  }

  const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response).error;
  throw std::runtime_error(std::string(methodName) + " failed: " + error.message);
}

auto awaitResponse(std::future<mcp::jsonrpc::Response> &future, std::string_view methodName) -> mcp::jsonrpc::Response
{
  if (future.wait_for(kOutboundRequestTimeout) != std::future_status::ready)
  {
    throw std::runtime_error(std::string(methodName) + " did not complete within timeout");
  }

  return future.get();
}

auto runOutboundAssertions(mcp::server::Server &server, const mcp::jsonrpc::RequestContext &context) -> void
{
  mcp::jsonrpc::Request samplingRequest;
  samplingRequest.id = std::int64_t {kSamplingRequestId};
  samplingRequest.method = "sampling/createMessage";
  samplingRequest.params = mcp::jsonrpc::JsonValue::object();
  (*samplingRequest.params)["maxTokens"] = std::int64_t {64};
  (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();

  mcp::jsonrpc::JsonValue samplingMessage = mcp::jsonrpc::JsonValue::object();
  samplingMessage["role"] = "user";
  samplingMessage["content"] = mcp::jsonrpc::JsonValue::object();
  samplingMessage["content"]["type"] = "text";
  samplingMessage["content"]["text"] = "cpp-server-sampling-check";
  (*samplingRequest.params)["messages"].push_back(std::move(samplingMessage));

  std::future<mcp::jsonrpc::Response> samplingFuture = server.sendRequest(context, std::move(samplingRequest));
  const mcp::jsonrpc::Response samplingResponse = awaitResponse(samplingFuture, "sampling/createMessage");
  throwIfErrorResponse(samplingResponse, "sampling/createMessage");
  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(samplingResponse))
  {
    throw std::runtime_error("sampling/createMessage did not return a success response");
  }

  const auto &samplingResult = std::get<mcp::jsonrpc::SuccessResponse>(samplingResponse).result;
  if (!samplingResult.is_object() || !samplingResult.contains("content") || !samplingResult["content"].is_object() || !samplingResult["content"].contains("text")
      || !samplingResult["content"]["text"].is_string())
  {
    throw std::runtime_error("sampling/createMessage response did not include content.text");
  }

  const std::string samplingText = samplingResult["content"]["text"].as<std::string>();
  if (samplingText != kExpectedSamplingResponseText)
  {
    throw std::runtime_error("sampling/createMessage returned unexpected text: " + samplingText);
  }

  mcp::jsonrpc::Request elicitationRequest;
  elicitationRequest.id = std::int64_t {kElicitationRequestId};
  elicitationRequest.method = "elicitation/create";
  elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["mode"] = "form";
  (*elicitationRequest.params)["message"] = "cpp-server-elicitation-check";
  (*elicitationRequest.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["type"] = "object";
  (*elicitationRequest.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["approved"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["approved"]["type"] = "boolean";
  (*elicitationRequest.params)["requestedSchema"]["properties"]["reason"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["reason"]["type"] = "string";
  (*elicitationRequest.params)["requestedSchema"]["required"] = mcp::jsonrpc::JsonValue::array();
  (*elicitationRequest.params)["requestedSchema"]["required"].push_back("approved");
  (*elicitationRequest.params)["requestedSchema"]["required"].push_back("reason");

  std::future<mcp::jsonrpc::Response> elicitationFuture = server.sendRequest(context, std::move(elicitationRequest));
  const mcp::jsonrpc::Response elicitationResponse = awaitResponse(elicitationFuture, "elicitation/create");
  throwIfErrorResponse(elicitationResponse, "elicitation/create");
  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(elicitationResponse))
  {
    throw std::runtime_error("elicitation/create did not return a success response");
  }

  const auto &elicitationResult = std::get<mcp::jsonrpc::SuccessResponse>(elicitationResponse).result;
  if (!elicitationResult.is_object() || !elicitationResult.contains("action") || !elicitationResult["action"].is_string())
  {
    throw std::runtime_error("elicitation/create response did not include action");
  }

  const std::string action = elicitationResult["action"].as<std::string>();
  if (action != "accept")
  {
    throw std::runtime_error("elicitation/create returned unexpected action: " + action);
  }

  if (!elicitationResult.contains("content") || !elicitationResult["content"].is_object())
  {
    throw std::runtime_error("elicitation/create response did not include content object");
  }

  const auto &content = elicitationResult["content"];
  if (!content.contains("approved") || !content["approved"].is_bool() || !content["approved"].as<bool>())
  {
    throw std::runtime_error("elicitation/create content.approved was not true");
  }

  if (!content.contains("reason") || !content["reason"].is_string())
  {
    throw std::runtime_error("elicitation/create content.reason was missing");
  }

  const std::string reason = content["reason"].as<std::string>();
  if (reason != kExpectedElicitationReason)
  {
    throw std::runtime_error("elicitation/create content.reason was unexpected: " + reason);
  }

  std::cerr << "cpp stdio integration server outbound sampling/elicitation assertions passed" << '\n';
}

}  // namespace

auto main(int /*argc*/, char ** /*argv*/) -> int
{
  try
  {
    // Shared registry to track server instances created by the runner's factory
    auto serverRegistry = std::make_shared<ServerRegistry>();

    auto makeServer = [&serverRegistry]() -> std::shared_ptr<mcp::server::Server>
    {
      mcp::lifecycle::session::ToolsCapability toolsCapability;
      mcp::lifecycle::session::ResourcesCapability resourcesCapability;
      mcp::lifecycle::session::PromptsCapability promptsCapability;

      mcp::ServerConfiguration configuration;
      configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);
      configuration.serverInfo = mcp::lifecycle::session::Implementation("cpp-integration-stdio-server", "1.0.0");
      configuration.instructions = "STDIO integration fixture server for reference SDK tests.";

      const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

      // Register this server in the registry
      {
        std::scoped_lock lock(serverRegistry->mutex);
        serverRegistry->servers.push_back(server);
      }

      mcp::ToolDefinition echoTool;
      echoTool.name = "cpp_echo";
      echoTool.description = "Echo text from arguments.text";
      echoTool.inputSchema = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["type"] = "object";
      echoTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["properties"]["text"]["type"] = "string";
      echoTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
      echoTool.inputSchema["required"].push_back("text");

      server->registerTool(std::move(echoTool),
                           [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                           {
                             mcp::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();
                             result.content.push_back(makeTextContent("cpp echo: " + context.arguments["text"].as<std::string>()));
                             return result;
                           });

      mcp::ResourceDefinition infoResource;
      infoResource.uri = "resource://cpp-stdio-server/info";
      infoResource.name = "cpp-stdio-server-info";
      infoResource.description = "Reference data exposed by the C++ STDIO integration fixture";
      infoResource.mimeType = "text/plain";

      server->registerResource(std::move(infoResource),
                               [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                               {
                                 return {
                                   mcp::ResourceContent::text("resource://cpp-stdio-server/info", "cpp stdio integration resource", std::string("text/plain")),
                                 };
                               });

      mcp::PromptDefinition prompt;
      prompt.name = "cpp_stdio_server_prompt";
      prompt.description = "Returns a prompt with the provided topic";

      mcp::PromptArgumentDefinition topicArgument;
      topicArgument.name = "topic";
      topicArgument.required = true;
      prompt.arguments.push_back(std::move(topicArgument));

      server->registerPrompt(std::move(prompt),
                             [](const mcp::PromptGetContext &context) -> mcp::PromptGetResult
                             {
                               mcp::PromptGetResult result;
                               result.description = "C++ STDIO integration prompt";

                               mcp::PromptMessage message;
                               message.role = "user";
                               message.content = mcp::jsonrpc::JsonValue::object();
                               message.content["type"] = "text";
                               message.content["text"] = "C++ stdio prompt topic: " + context.arguments["topic"].as<std::string>();
                               result.messages.push_back(std::move(message));
                               return result;
                             });

      return server;
    };

    mcp::server::StdioServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.allowStderrLogs = true;

    mcp::server::StdioServerRunner runner(makeServer, std::move(runnerOptions));

    OutboundAssertionsState outboundAssertions;

    // Set up a polling worker to wait for session state and run outbound assertions
    // The worker polls for the first session to reach kOperating state (after notifications/initialized)
    outboundAssertions.worker = std::thread(
      [&runner, &serverRegistry, &outboundAssertions]() -> void
      {
        try
        {
          const auto startTime = std::chrono::steady_clock::now();
          std::shared_ptr<mcp::server::Server> targetServer;

          // Poll for a server that has reached kOperating state
          while (true)
          {
            const auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed > kSessionStateWaitTimeout)
            {
              throw std::runtime_error("timeout waiting for session to reach kOperating state");
            }

            // Find a server that has reached kOperating state
            {
              std::scoped_lock lock(serverRegistry->mutex);
              for (const auto &weakServer : serverRegistry->servers)
              {
                auto server = weakServer.lock();
                if (server)
                {
                  const auto sessionState = server->session()->state();
                  if (sessionState == mcp::lifecycle::session::SessionState::kOperating)
                  {
                    targetServer = std::move(server);
                    break;
                  }
                }
              }
            }

            if (targetServer)
            {
              break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }

          if (!targetServer)
          {
            throw std::runtime_error("no server reached kOperating state");
          }

          // Run the outbound assertions with an empty context
          // The runner's outbound message sender handles routing using stored session ID
          runOutboundAssertions(*targetServer, mcp::jsonrpc::RequestContext {});
          outboundAssertions.passed.store(true);
        }
        catch (const std::exception &error)
        {
          std::scoped_lock lock(outboundAssertions.mutex);
          outboundAssertions.failureReason = error.what();
        }
        catch (...)
        {
          std::scoped_lock lock(outboundAssertions.mutex);
          outboundAssertions.failureReason = "unknown outbound assertion failure";
        }

        outboundAssertions.completed.store(true);
      });

    // Run the server - blocks until EOF on stdin
    runner.run();

    if (outboundAssertions.worker.joinable())
    {
      outboundAssertions.worker.join();
    }

    // Check if outbound assertions were started
    if (!outboundAssertions.completed.load())
    {
      // The worker might still be running or not started - this is a timeout case
      std::cerr << "cpp_stdio_server_fixture failed: outbound sampling/elicitation assertions did not complete" << '\n';
      return 3;
    }

    if (!outboundAssertions.passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(outboundAssertions.mutex);
        failureReason = outboundAssertions.failureReason;
      }

      std::cerr << "cpp_stdio_server_fixture failed: outbound sampling/elicitation assertions failed";
      if (failureReason.has_value())
      {
        std::cerr << " (" << *failureReason << ')';
      }
      std::cerr << '\n';
      return 3;
    }

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_stdio_server_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
