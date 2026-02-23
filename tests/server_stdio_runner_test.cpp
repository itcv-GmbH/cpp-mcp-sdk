#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/server/all.hpp>

namespace
{

// Helper to create an initialize request JSON with the latest protocol version
static auto makeInitializeRequestJson() -> std::string
{
  return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":")" + std::string(mcp::kLatestProtocolVersion)
    + R"(","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}}})";
}

// Helper to create a notifications/initialized notification JSON
static auto makeInitializedNotificationJson() -> std::string
{
  return R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
}

// Minimal ServerFactory that creates a Server with at least one tool registered
// This is required for initialize to succeed (server needs to have capabilities)
static auto createMinimalServer() -> std::shared_ptr<mcp::server::Server>
{
  auto server = mcp::server::Server::create();

  // Register at least one tool so the server has tools capability
  mcp::server::ToolDefinition toolDef;
  toolDef.name = "ping";
  toolDef.description = "A simple ping tool for testing";
  toolDef.inputSchema = mcp::jsonrpc::JsonValue::object();
  toolDef.inputSchema["type"] = "object";
  toolDef.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();

  server->registerTool(std::move(toolDef),
                       [](const mcp::server::ToolCallContext &) -> mcp::server::CallToolResult
                       {
                         mcp::server::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(mcp::jsonrpc::JsonValue::object());
                         result.content[0]["type"] = "text";
                         result.content[0]["text"] = "pong";
                         return result;
                       });

  return server;
}

}  // namespace

TEST_CASE("StdioServerRunner handles valid initialize flow", "[server][stdio_runner]")
{
  // Create input: initialize request + notification + EOF
  std::ostringstream inputStream;
  inputStream << makeInitializeRequestJson() << '\n';
  inputStream << makeInitializedNotificationJson() << '\n';

  std::istringstream input(inputStream.str());
  std::ostringstream output;
  std::ostringstream stderr;

  // Create runner with factory that produces minimal server
  mcp::server::StdioServerRunner runner(createMinimalServer);
  runner.run(input, output, stderr);

  // Parse stdout - should contain exactly one JSON-RPC response (the initialize response)
  const std::string outputStr = output.str();
  REQUIRE_FALSE(outputStr.empty());

  const auto message = mcp::jsonrpc::parseMessage(outputStr);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message));

  const auto &response = std::get<mcp::jsonrpc::SuccessResponse>(message);
  // RequestId is a std::variant<std::int64_t, std::string>, extract the value for comparison
  REQUIRE(std::get<std::int64_t>(response.id) == std::int64_t {1});
  REQUIRE(response.result.is_object());
}

TEST_CASE("StdioServerRunner handles malformed JSON with parse error", "[server][stdio_runner]")
{
  // Create input: initialize request + notification + invalid JSON + EOF
  std::ostringstream inputStream;
  inputStream << makeInitializeRequestJson() << '\n';
  inputStream << makeInitializedNotificationJson() << '\n';
  inputStream << "not valid json" << '\n';

  std::istringstream input(inputStream.str());
  std::ostringstream output;
  std::ostringstream stderr;

  mcp::server::StdioServerRunner runner(createMinimalServer);
  runner.run(input, output, stderr);

  // Parse stdout - should contain two JSON-RPC messages:
  // 1. initialize response
  // 2. parse error response for invalid JSON
  const std::string outputStr = output.str();
  REQUIRE_FALSE(outputStr.empty());

  std::istringstream outputLines(outputStr);
  std::string line;

  // First line: initialize response
  REQUIRE(std::getline(outputLines, line));
  REQUIRE_FALSE(line.empty());
  {
    const auto message = mcp::jsonrpc::parseMessage(line);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message));
  }

  // Second line: parse error response
  REQUIRE(std::getline(outputLines, line));
  REQUIRE_FALSE(line.empty());
  {
    const auto message = mcp::jsonrpc::parseMessage(line);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(message));
    const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(message);
    // Parse error should have hasUnknownId == true (id: null)
    REQUIRE(errorResponse.hasUnknownId);
    REQUIRE(errorResponse.error.code == static_cast<std::int64_t>(mcp::JsonRpcErrorCode::kParseError));
  }
}

TEST_CASE("StdioServerRunner never writes diagnostics to stdout", "[server][stdio_runner]")
{
  // Create input: initialize request + notification + invalid JSON + EOF
  std::ostringstream inputStream;
  inputStream << makeInitializeRequestJson() << '\n';
  inputStream << makeInitializedNotificationJson() << '\n';
  inputStream << "not valid json" << '\n';

  std::istringstream input(inputStream.str());
  std::ostringstream output;
  std::ostringstream stderr;

  mcp::server::StdioServerRunner runner(createMinimalServer);
  runner.run(input, output, stderr);

  // stdout should contain ONLY valid JSON-RPC messages (newline-delimited)
  const std::string outputStr = output.str();
  std::istringstream outputLines(outputStr);
  std::string line;

  while (std::getline(outputLines, line))
  {
    if (line.empty())
    {
      continue;
    }

    // Each line must be a valid JSON-RPC message (no diagnostic text)
    REQUIRE_NOTHROW(mcp::jsonrpc::parseMessage(line));
  }

  // Verify stderr received diagnostics (for the malformed JSON case)
  // Note: stderr may or may not contain logs depending on error handling path
}

TEST_CASE("StdioServerRunner writes diagnostics to stderr when allowed", "[server][stdio_runner]")
{
  // Create input: initialize request + notification + invalid JSON + EOF
  std::ostringstream inputStream;
  inputStream << makeInitializeRequestJson() << '\n';
  inputStream << makeInitializedNotificationJson() << '\n';
  inputStream << "not valid json" << '\n';

  std::istringstream input(inputStream.str());
  std::ostringstream output;
  std::ostringstream stderr;

  // Enable stderr logging
  mcp::server::StdioServerRunnerOptions options;
  options.transportOptions.allowStderrLogs = true;

  mcp::server::StdioServerRunner runner(createMinimalServer, options);
  runner.run(input, output, stderr);

  // With allowStderrLogs=true, stderr should contain diagnostics
  const std::string stderrStr = stderr.str();
  // stderr may contain parse error diagnostics - verify it's not empty
  // (exact content depends on error handling path)
  REQUIRE_FALSE(stderrStr.empty());
}
