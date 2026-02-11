#include <chrono>
#include <cstdint>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/stdio.hpp>

#ifndef MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH
#  error "MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH must be defined by CMake."
#endif

TEST_CASE("Stdio subprocess client spawns helper server and exchanges messages", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocessSpawnOptions spawnOptions;
  spawnOptions.argv = {MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH};
  spawnOptions.stderrMode = mcp::transport::StdioClientStderrMode::kCapture;

  mcp::transport::StdioSubprocess subprocess = mcp::transport::StdioTransport::spawnSubprocess(spawnOptions);
  REQUIRE(subprocess.valid());

  subprocess.writeLine(R"({"jsonrpc":"2.0","id":11,"method":"ping"})");

  std::string responseLine;
  REQUIRE(subprocess.readLine(responseLine));

  const mcp::jsonrpc::Message responseMessage = mcp::jsonrpc::parseMessage(responseLine);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseMessage));

  const mcp::jsonrpc::SuccessResponse &success = std::get<mcp::jsonrpc::SuccessResponse>(responseMessage);
  REQUIRE(std::holds_alternative<std::int64_t>(success.id));
  REQUIRE(std::get<std::int64_t>(success.id) == 11);

  REQUIRE(subprocess.shutdown(mcp::transport::StdioSubprocessShutdownOptions {
    .waitForExitTimeout = std::chrono::milliseconds {1000},
    .waitAfterTerminateTimeout = std::chrono::milliseconds {1000},
  }));

  const std::string capturedStderr = subprocess.capturedStderr();
  REQUIRE(capturedStderr.find("helper-server-started") != std::string::npos);
}

TEST_CASE("Stdio subprocess client supports ignored stderr mode", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocessSpawnOptions spawnOptions;
  spawnOptions.argv = {MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH};
  spawnOptions.stderrMode = mcp::transport::StdioClientStderrMode::kIgnore;

  mcp::transport::StdioSubprocess subprocess = mcp::transport::StdioTransport::spawnSubprocess(spawnOptions);
  REQUIRE(subprocess.valid());

  subprocess.closeStdin();
  REQUIRE(subprocess.waitForExit(std::chrono::milliseconds {2000}));
}
