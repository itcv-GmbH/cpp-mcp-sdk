#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/all.hpp>

#ifndef MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH
#  error "MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH must be defined by CMake."
#endif

namespace
{

auto makeSpawnOptions(mcp::transport::StdioClientStderrMode stderrMode) -> mcp::transport::StdioSubprocessSpawnOptions
{
  mcp::transport::StdioSubprocessSpawnOptions spawnOptions;
  spawnOptions.argv = {MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH};
  spawnOptions.stderrMode = stderrMode;
  return spawnOptions;
}

auto requirePingRoundTrip(mcp::transport::StdioSubprocess &subprocess, std::int64_t requestId) -> void
{
  subprocess.writeLine(std::string(R"({"jsonrpc":"2.0","id":)") + std::to_string(requestId) + R"(,"method":"ping"})");

  std::string responseLine;
  REQUIRE(subprocess.readLine(responseLine));

  const mcp::jsonrpc::Message responseMessage = mcp::jsonrpc::parseMessage(responseLine);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseMessage));

  const mcp::jsonrpc::SuccessResponse &success = std::get<mcp::jsonrpc::SuccessResponse>(responseMessage);
  REQUIRE(std::holds_alternative<std::int64_t>(success.id));
  REQUIRE(std::get<std::int64_t>(success.id) == requestId);
}

auto toLowerCopy(std::string text) -> std::string
{
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) -> char { return static_cast<char>(std::tolower(character)); });
  return text;
}

auto isActionableSpawnError(const std::string &message, const std::string &executablePath) -> bool
{
  if (message.empty())
  {
    return false;
  }

  if (message.find(executablePath) != std::string::npos)
  {
    return true;
  }

  const std::string normalized = toLowerCopy(message);
  return normalized.find("no such file") != std::string::npos || normalized.find("not found") != std::string::npos || normalized.find("cannot find") != std::string::npos
    || normalized.find("failed") != std::string::npos || normalized.find("executable") != std::string::npos;
}

}  // namespace

TEST_CASE("Stdio subprocess client spawns helper server and exchanges messages", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocess subprocess = mcp::transport::StdioTransport::spawnSubprocess(makeSpawnOptions(mcp::transport::StdioClientStderrMode::kCapture));
  REQUIRE(subprocess.valid());

  requirePingRoundTrip(subprocess, 11);

  mcp::transport::StdioSubprocessShutdownOptions shutdownOpts1;
  shutdownOpts1.waitForExitTimeout = std::chrono::milliseconds {1000};
  shutdownOpts1.waitAfterTerminateTimeout = std::chrono::milliseconds {1000};
  REQUIRE(subprocess.shutdown(shutdownOpts1));

  const std::string capturedStderr = subprocess.capturedStderr();
  REQUIRE(capturedStderr.find("helper-server-started") != std::string::npos);
}

TEST_CASE("Stdio subprocess client supports ignored stderr mode", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocess subprocess = mcp::transport::StdioTransport::spawnSubprocess(makeSpawnOptions(mcp::transport::StdioClientStderrMode::kIgnore));
  REQUIRE(subprocess.valid());

  subprocess.closeStdin();
  REQUIRE(subprocess.waitForExit(std::chrono::milliseconds {2000}));
}

TEST_CASE("Stdio subprocess client supports forwarded stderr mode while exchanging ping", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocess subprocess = mcp::transport::StdioTransport::spawnSubprocess(makeSpawnOptions(mcp::transport::StdioClientStderrMode::kForward));
  REQUIRE(subprocess.valid());

  requirePingRoundTrip(subprocess, 27);

  mcp::transport::StdioSubprocessShutdownOptions shutdownOpts2;
  shutdownOpts2.waitForExitTimeout = std::chrono::milliseconds {1000};
  shutdownOpts2.waitAfterTerminateTimeout = std::chrono::milliseconds {1000};
  REQUIRE(subprocess.shutdown(shutdownOpts2));
}

TEST_CASE("Stdio subprocess client validates spawn options", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocessSpawnOptions emptyArgvOptions;
  try
  {
    auto subprocess = mcp::transport::StdioTransport::spawnSubprocess(emptyArgvOptions);
    static_cast<void>(subprocess);
    FAIL("Expected empty argv to throw std::invalid_argument");
  }
  catch (const std::invalid_argument &error)
  {
    const std::string message = error.what();
    REQUIRE(message.find("argv") != std::string::npos);
    REQUIRE(message.find("executable") != std::string::npos);
  }

  mcp::transport::StdioSubprocessSpawnOptions invalidPathOptions;
  invalidPathOptions.argv = {std::string(MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH) + ".missing"};
  const std::string invalidPath = invalidPathOptions.argv.front();

  try
  {
    auto subprocess = mcp::transport::StdioTransport::spawnSubprocess(invalidPathOptions);
    static_cast<void>(subprocess);
    FAIL("Expected invalid executable path to throw");
  }
  catch (const std::exception &error)
  {
    REQUIRE(isActionableSpawnError(error.what(), invalidPath));
  }
}

TEST_CASE("Stdio subprocess client shutdown is idempotent", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocess subprocess = mcp::transport::StdioTransport::spawnSubprocess(makeSpawnOptions(mcp::transport::StdioClientStderrMode::kCapture));
  REQUIRE(subprocess.valid());

  requirePingRoundTrip(subprocess, 31);

  mcp::transport::StdioSubprocessShutdownOptions shutdownOptions;
  shutdownOptions.waitForExitTimeout = std::chrono::milliseconds {1000};
  shutdownOptions.waitAfterTerminateTimeout = std::chrono::milliseconds {1000};

  REQUIRE(subprocess.shutdown(shutdownOptions));
  REQUIRE(subprocess.shutdown(shutdownOptions));
}

TEST_CASE("Stdio subprocess client waitForExit timeout returns false", "[transport][stdio][subprocess]")
{
  mcp::transport::StdioSubprocess subprocess = mcp::transport::StdioTransport::spawnSubprocess(makeSpawnOptions(mcp::transport::StdioClientStderrMode::kIgnore));
  REQUIRE(subprocess.valid());

  requirePingRoundTrip(subprocess, 52);

  bool exited = true;
  REQUIRE_NOTHROW(exited = subprocess.waitForExit(std::chrono::milliseconds {50}));
  REQUIRE_FALSE(exited);

  mcp::transport::StdioSubprocessShutdownOptions shutdownOpts3;
  shutdownOpts3.waitForExitTimeout = std::chrono::milliseconds {1000};
  shutdownOpts3.waitAfterTerminateTimeout = std::chrono::milliseconds {1000};
  REQUIRE(subprocess.shutdown(shutdownOpts3));
}
