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

#include <mcp/server/all.hpp>
#include <mcp/server/stdio_runner.hpp>
#include "mcp/jsonrpc/types.hpp"
#include "mcp/server/server.hpp"
#include "mcp/lifecycle/session/tools_capability.hpp"
#include "mcp/lifecycle/session/resources_capability.hpp"
#include "mcp/lifecycle/session/prompts_capability.hpp"
#include "mcp/lifecycle/session/tasks_capability.hpp"
#include "mcp/server/server_configuration.hpp"
#include "mcp/server/tool_definition.hpp"
#include "mcp/server/tool_call_context.hpp"
#include "mcp/server/call_tool_result.hpp"
#include "mcp/server/resource_definition.hpp"
#include "mcp/server/resource_read_context.hpp"
#include "mcp/server/resource_content.hpp"
#include "mcp/server/prompt_definition.hpp"
#include "mcp/server/prompt_argument_definition.hpp"
#include "mcp/server/prompt_get_context.hpp"
#include "mcp/server/prompt_get_result.hpp"
#include "mcp/server/prompt_message.hpp"
#include "mcp/server/stdio_server_runner_options.hpp"

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

auto createServer() -> std::shared_ptr<mcp::server::Server>
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

  mcp::server::ServerConfiguration configuration;
  configuration.capabilities =
    mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, tasksCapability, std::nullopt);
  configuration.serverInfo = mcp::lifecycle::session::Implementation("example-stdio-server", "1.0.0");
  configuration.instructions = "Use tools for generated output and read resources for static context.";

  const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

  mcp::server::ToolDefinition echoTool;
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
                       [](const mcp::server::ToolCallContext &context) -> mcp::server::CallToolResult
                       {
                         mcp::server::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(makeTextContent("echo: " + context.arguments["text"].as<std::string>()));
                         return result;
                       });

  mcp::server::ToolDefinition delayedTool;
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
                       [](const mcp::server::ToolCallContext &context) -> mcp::server::CallToolResult
                       {
                         std::this_thread::sleep_for(std::chrono::milliseconds(kDelayedEchoMilliseconds));

                         mcp::server::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(makeTextContent("delayed: " + context.arguments["text"].as<std::string>()));
                         return result;
                       });

  mcp::server::ResourceDefinition resource;
  resource.uri = "resource://server/about";
  resource.name = "about";
  resource.description = "Server metadata";
  resource.mimeType = "text/plain";

  server->registerResource(
    std::move(resource),
    [](const mcp::server::ResourceReadContext &) -> std::vector<mcp::server::ResourceContent>
    {
      return {
        mcp::server::ResourceContent::text("resource://server/about", "Example stdio server with tools/resources/prompts/tasks support.", std::string("text/plain")),
      };
    });

  mcp::server::PromptDefinition prompt;
  prompt.name = "explain-topic";
  prompt.description = "Generate a one-line explanatory prompt";

  mcp::server::PromptArgumentDefinition topicArgument;
  topicArgument.name = "topic";
  topicArgument.required = true;
  prompt.arguments.push_back(std::move(topicArgument));

  server->registerPrompt(std::move(prompt),
                         [](const mcp::server::PromptGetContext &context) -> mcp::server::PromptGetResult
                         {
                           mcp::server::PromptGetResult result;
                           result.description = "Single user message prompt";

                           mcp::server::PromptMessage message;
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
    mcp::server::StdioServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.allowStderrLogs = false;

    mcp::server::StdioServerRunner runner(createServer, runnerOptions);

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
