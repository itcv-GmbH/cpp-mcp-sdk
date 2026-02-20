#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>

namespace
{

constexpr std::int64_t kDelayedEchoMilliseconds = 300;

auto makeTextContent(const std::string &text) -> mcp::jsonrpc::JsonValue  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = text;
  return content;
}

auto writeMessage(const mcp::jsonrpc::Message &message) -> void  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  std::cout << mcp::jsonrpc::serializeMessage(message) << '\n';
  std::cout.flush();
}

auto writeResponse(const mcp::jsonrpc::Response &response) -> void  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
  {
    writeMessage(mcp::jsonrpc::Message {std::get<mcp::jsonrpc::SuccessResponse>(response)});
    return;
  }

  writeMessage(mcp::jsonrpc::Message {std::get<mcp::jsonrpc::ErrorResponse>(response)});
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main() -> int
{
  mcp::ToolsCapability toolsCapability;
  toolsCapability.listChanged = true;

  mcp::ResourcesCapability resourcesCapability;
  resourcesCapability.listChanged = true;

  mcp::PromptsCapability promptsCapability;
  promptsCapability.listChanged = true;

  mcp::TasksCapability tasksCapability;
  tasksCapability.toolsCall = true;
  tasksCapability.list = true;
  tasksCapability.cancel = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, tasksCapability, std::nullopt);
  configuration.serverInfo = mcp::Implementation("example-stdio-server", "1.0.0");
  configuration.instructions = "Use tools for generated output and read resources for static context.";

  const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

  mcp::ToolDefinition echoTool;
  echoTool.name = "echo";
  echoTool.description = "Echo text from arguments.text";
  echoTool.execution = mcp::jsonrpc::JsonValue::object();
  (*echoTool.execution)["taskSupport"] = "optional";
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
                         result.content.push_back(makeTextContent("echo: " + context.arguments["text"].as<std::string>()));
                         return result;
                       });

  mcp::ToolDefinition delayedTool;
  delayedTool.name = "delayed_echo";
  delayedTool.description = "Sleep briefly, then echo arguments.text";
  delayedTool.execution = mcp::jsonrpc::JsonValue::object();
  (*delayedTool.execution)["taskSupport"] = "optional";
  delayedTool.inputSchema = mcp::jsonrpc::JsonValue::object();
  delayedTool.inputSchema["type"] = "object";
  delayedTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
  delayedTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
  delayedTool.inputSchema["properties"]["text"]["type"] = "string";
  delayedTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
  delayedTool.inputSchema["required"].push_back("text");

  server->registerTool(std::move(delayedTool),
                       [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                       {
                         std::this_thread::sleep_for(std::chrono::milliseconds(kDelayedEchoMilliseconds));

                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(makeTextContent("delayed: " + context.arguments["text"].as<std::string>()));
                         return result;
                       });

  mcp::ResourceDefinition resource;
  resource.uri = "resource://server/about";
  resource.name = "about";
  resource.description = "Server metadata";
  resource.mimeType = "text/plain";

  server->registerResource(std::move(resource),
                           [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                           {
                             return {
                               mcp::ResourceContent::text("resource://server/about", "Example stdio server with tools/resources/prompts/tasks support.", std::string("text/plain")),
                             };
                           });

  mcp::PromptDefinition prompt;
  prompt.name = "explain-topic";
  prompt.description = "Generate a one-line explanatory prompt";

  mcp::PromptArgumentDefinition topicArgument;
  topicArgument.name = "topic";
  topicArgument.required = true;
  prompt.arguments.push_back(std::move(topicArgument));

  server->registerPrompt(std::move(prompt),
                         [](const mcp::PromptGetContext &context) -> mcp::PromptGetResult
                         {
                           mcp::PromptGetResult result;
                           result.description = "Single user message prompt";

                           mcp::PromptMessage message;
                           message.role = "user";
                           message.content = mcp::jsonrpc::JsonValue::object();
                           message.content["type"] = "text";
                           message.content["text"] = "Explain this topic in one paragraph: " + context.arguments["topic"].as<std::string>();
                           result.messages.push_back(std::move(message));
                           return result;
                         });

  server->setOutboundMessageSender([](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Message &message) -> void { writeMessage(message); });

  std::cerr << "stdio_server: waiting for newline-delimited JSON-RPC messages on stdin" << '\n';
  std::cerr.flush();

  std::string line;
  while (std::getline(std::cin, line))
  {
    if (line.empty())
    {
      continue;
    }

    try
    {
      const mcp::jsonrpc::Message message = mcp::jsonrpc::parseMessage(line);
      const mcp::jsonrpc::RequestContext context {};

      if (std::holds_alternative<mcp::jsonrpc::Request>(message))
      {
        const mcp::jsonrpc::Response response = server->handleRequest(context, std::get<mcp::jsonrpc::Request>(message)).get();
        writeResponse(response);
        continue;
      }

      if (std::holds_alternative<mcp::jsonrpc::Notification>(message))
      {
        server->handleNotification(context, std::get<mcp::jsonrpc::Notification>(message));
        continue;
      }

      if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message))
      {
        const auto response = mcp::jsonrpc::Response {std::get<mcp::jsonrpc::SuccessResponse>(message)};
        static_cast<void>(server->handleResponse(context, response));
        continue;
      }

      const auto response = mcp::jsonrpc::Response {std::get<mcp::jsonrpc::ErrorResponse>(message)};
      static_cast<void>(server->handleResponse(context, response));
    }
    catch (const std::exception &error)
    {
      std::cerr << "stdio_server parse/dispatch error: " << error.what() << '\n';
      std::cerr.flush();

      mcp::jsonrpc::ErrorResponse parseError;
      parseError.id = std::nullopt;
      parseError.error = mcp::jsonrpc::makeParseError(std::nullopt, "Failed to parse or dispatch JSON-RPC message");
      writeMessage(mcp::jsonrpc::Message {parseError});
    }
  }

  return 0;
}
