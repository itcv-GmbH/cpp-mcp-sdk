#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/stdio_runner.hpp>
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

}  // namespace

auto createServer() -> std::shared_ptr<mcp::Server>
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;
  toolsCapability.listChanged = true;

  mcp::lifecycle::session::ResourcesCapability resourcesCapability;
  resourcesCapability.listChanged = true;

  mcp::lifecycle::session::PromptsCapability promptsCapability;
  promptsCapability.listChanged = true;

  mcp::lifecycle::session::TasksCapability tasksCapability;
  tasksCapability.toolsCall = true;
  tasksCapability.list = true;
  tasksCapability.cancel = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, tasksCapability, std::nullopt);
  configuration.serverInfo = mcp::lifecycle::session::Implementation("example-stdio-server", "1.0.0");
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

  return server;
}

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main() -> int
{
  try
  {
    mcp::StdioServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.allowStderrLogs = false;

    mcp::StdioServerRunner runner(createServer, runnerOptions);

    std::cerr << "stdio_server: waiting for JSON-RPC messages on stdin" << '\n';
    std::cerr.flush();

    runner.run();

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "stdio_server error: " << error.what() << '\n';
    return 1;
  }
}
