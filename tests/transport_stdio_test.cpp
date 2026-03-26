#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/sdk/errors.hpp>

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <mcp/jsonrpc/all.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/transport/all.hpp>

static auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

static auto registerPingHandler(mcp::jsonrpc::Router &router) -> void
{
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse response;
                                  response.id = request.id;
                                  response.result = mcp::jsonrpc::JsonValue::object();
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {response});
                                });
}

class StandardStreamRedirect
{
public:
  StandardStreamRedirect(std::istream &input, std::ostream &output, std::ostream &error)
    : oldInBuffer_(std::cin.rdbuf(input.rdbuf()))
    , oldOutBuffer_(std::cout.rdbuf(output.rdbuf()))
    , oldErrBuffer_(std::cerr.rdbuf(error.rdbuf()))
  {
  }

  ~StandardStreamRedirect()
  {
    std::cin.rdbuf(oldInBuffer_);
    std::cout.rdbuf(oldOutBuffer_);
    std::cerr.rdbuf(oldErrBuffer_);
  }

  StandardStreamRedirect(const StandardStreamRedirect &) = delete;
  auto operator=(const StandardStreamRedirect &) -> StandardStreamRedirect & = delete;

private:
  std::streambuf *oldInBuffer_;
  std::streambuf *oldOutBuffer_;
  std::streambuf *oldErrBuffer_;
};

TEST_CASE("Stdio transport rejects embedded CR and CRLF framing violations", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;
  const mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = false};

  const std::string withEmbeddedCr = std::string(R"({"jsonrpc":"2.0","id":1,"method":"pi")") + '\r' + R"(ng"})";
  const bool acceptedEmbeddedCr = mcp::transport::StdioTransport::routeIncomingLine(router, withEmbeddedCr, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(acceptedEmbeddedCr);
  REQUIRE(stdoutCapture.str().empty());

  const std::string withEmbeddedCrLf = std::string(R"({"jsonrpc":"2.0","id":2,"method":"pi")") + "\r\n" + R"(ng"})";
  const bool acceptedEmbeddedCrLf = mcp::transport::StdioTransport::routeIncomingLine(router, withEmbeddedCrLf, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(acceptedEmbeddedCrLf);

  const std::string withCrLfTerminator = std::string(R"({"jsonrpc":"2.0","id":3,"method":"ping"})") + '\r';
  const bool acceptedCrLfTerminator = mcp::transport::StdioTransport::routeIncomingLine(router, withCrLfTerminator, stdoutCapture, &stderrCapture, options);
  REQUIRE(acceptedCrLfTerminator);

  std::istringstream routedStream(stdoutCapture.str());
  std::string routedLine;
  REQUIRE(std::getline(routedStream, routedLine));
  REQUIRE_NOTHROW(mcp::jsonrpc::parseMessage(routedLine));
}

TEST_CASE("Stdio transport rejects invalid UTF-8 inbound data", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;
  const mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = false};

  const std::string invalidUtf8 = std::string(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"text":")") + std::string("\xC3\x28") + R"("}})";

  const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, invalidUtf8, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(accepted);
  REQUIRE(stdoutCapture.str().empty());
  REQUIRE(stderrCapture.str().find("non UTF-8") != std::string::npos);
}

TEST_CASE("Stdio attach rejects partial EOF frame and does not route it", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::istringstream simulatedServerStdout(R"({"jsonrpc":"2.0","id":77,"method":"ping"})");
  std::ostringstream simulatedServerStdin;
  std::ostringstream stderrCapture;

  const mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = true};

  {
    StandardStreamRedirect redirect(simulatedServerStdout, simulatedServerStdin, stderrCapture);
    mcp::transport::StdioTransport::attach(router, simulatedServerStdout, simulatedServerStdin, options);
  }

  REQUIRE(simulatedServerStdin.str().empty());
  REQUIRE(stderrCapture.str().find("unterminated EOF frame") != std::string::npos);
}

TEST_CASE("Stdio run keeps stdout MCP-only and logs diagnostics to stderr", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::istringstream stdinInput;
  stdinInput.str(std::string(R"({"jsonrpc":"2.0","id":41,"method":"ping"})") + "\n" + "not-json\n" + R"({"jsonrpc":"2.0","id":42,"method":"ping"})");

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;

  {
    StandardStreamRedirect redirect(stdinInput, stdoutCapture, stderrCapture);
    mcp::transport::StdioTransport::run(router, mcp::transport::StdioServerOptions {.allowStderrLogs = true});
  }

  std::istringstream stdoutStream(stdoutCapture.str());
  std::string outputLine;
  bool sawOutput = false;
  while (std::getline(stdoutStream, outputLine))
  {
    if (outputLine.empty())
    {
      continue;
    }

    sawOutput = true;
    REQUIRE_NOTHROW(mcp::jsonrpc::parseMessage(outputLine));
  }

  REQUIRE(sawOutput);
  REQUIRE(stderrCapture.str().find("stdio transport parse failed") != std::string::npos);
  REQUIRE(stderrCapture.str().find("unterminated EOF frame") != std::string::npos);
}

TEST_CASE("Stdio transport rejects embedded LF framing violations", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;
  const mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = false};

  // Test embedded LF in the middle of a JSON object
  const std::string withEmbeddedLf = std::string(R"({"jsonrpc":"2.0","id":1,"method":"pi")") + '\n' + R"(ng"})";
  const bool acceptedEmbeddedLf = mcp::transport::StdioTransport::routeIncomingLine(router, withEmbeddedLf, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(acceptedEmbeddedLf);
  REQUIRE(stdoutCapture.str().empty());
  REQUIRE(stderrCapture.str().find("framing") != std::string::npos);
}

TEST_CASE("Stdio transport emits JSON-RPC parse errors when emitParseErrors is true", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;
  const mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = true};

  SECTION("Parse error for invalid JSON")
  {
    const std::string invalidJson = R"({"jsonrpc":"2.0","id":1,"method":)";
    const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, invalidJson, stdoutCapture, &stderrCapture, options);
    REQUIRE_FALSE(accepted);

    // stdout should contain a JSON-RPC error response
    REQUIRE_FALSE(stdoutCapture.str().empty());
    const auto message = mcp::jsonrpc::parseMessage(stdoutCapture.str());
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(message));
    const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(message);
    REQUIRE(errorResponse.hasUnknownId);
    REQUIRE(errorResponse.error.code == static_cast<std::int64_t>(mcp::JsonRpcErrorCode::kParseError));
  }

  SECTION("Parse error for non-object JSON")
  {
    stdoutCapture.str("");
    stderrCapture.str("");

    const std::string nonObjectJson = R"("just a string")";
    const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, nonObjectJson, stdoutCapture, &stderrCapture, options);
    REQUIRE_FALSE(accepted);

    REQUIRE_FALSE(stdoutCapture.str().empty());
    const auto message = mcp::jsonrpc::parseMessage(stdoutCapture.str());
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(message));
  }

  SECTION("Parse error for missing jsonrpc field")
  {
    stdoutCapture.str("");
    stderrCapture.str("");

    const std::string missingVersion = R"({"id":1,"method":"ping"})";
    const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, missingVersion, stdoutCapture, &stderrCapture, options);
    REQUIRE_FALSE(accepted);

    REQUIRE_FALSE(stdoutCapture.str().empty());
    const auto message = mcp::jsonrpc::parseMessage(stdoutCapture.str());
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(message));
  }
}

TEST_CASE("Stdio transport handles max message size limit via direct framing", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;

  // Set a very small max message size to trigger the limit
  mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = true};
  constexpr std::size_t kSmallMaxMessageSize = 50U;
  options.limits.maxMessageSizeBytes = kSmallMaxMessageSize;

  const std::string oversizedMessage = R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"extra":"data"}})";
  REQUIRE(oversizedMessage.size() > options.limits.maxMessageSizeBytes);

  const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, oversizedMessage, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(accepted);

  // stderr should have the size limit message
  REQUIRE(stderrCapture.str().find("max message size") != std::string::npos);

  // stdout should contain a parse error response (since emitParseErrors=true)
  REQUIRE_FALSE(stdoutCapture.str().empty());
  const auto message = mcp::jsonrpc::parseMessage(stdoutCapture.str());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(message));
  const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(message);
  REQUIRE(errorResponse.hasUnknownId);
  REQUIRE(errorResponse.error.code == static_cast<std::int64_t>(mcp::JsonRpcErrorCode::kParseError));
}

TEST_CASE("Stdio transport handles max message size without emitting parse errors", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;

  // Set a very small max message size but disable parse error emission
  mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = false};
  constexpr std::size_t kSmallMaxMessageSize = 50U;
  options.limits.maxMessageSizeBytes = kSmallMaxMessageSize;

  const std::string oversizedMessage = R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"extra":"data"}})";
  REQUIRE(oversizedMessage.size() > options.limits.maxMessageSizeBytes);

  const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, oversizedMessage, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(accepted);

  // stderr should still have the error message
  REQUIRE(stderrCapture.str().find("max message size") != std::string::npos);

  // stdout should be empty (no parse error response emitted)
  REQUIRE(stdoutCapture.str().empty());
}

TEST_CASE("Stdio transport handles empty lines and whitespace-only lines", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;
  const mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = false};

  SECTION("Empty line is rejected")
  {
    const std::string emptyLine;
    const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, emptyLine, stdoutCapture, &stderrCapture, options);
    REQUIRE_FALSE(accepted);
    // Empty line fails to parse, should log to stderr
    REQUIRE(stderrCapture.str().find("parse") != std::string::npos);
  }

  SECTION("Whitespace-only line is rejected")
  {
    stdoutCapture.str("");
    stderrCapture.str("");

    const std::string whitespaceOnly = "   \t   ";
    const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, whitespaceOnly, stdoutCapture, &stderrCapture, options);
    REQUIRE_FALSE(accepted);
    REQUIRE(stderrCapture.str().find("parse") != std::string::npos);
  }

  SECTION("Valid message after empty line works correctly")
  {
    stdoutCapture.str("");
    stderrCapture.str("");

    // First, an empty line should be rejected
    const std::string emptyLine;
    const bool emptyAccepted = mcp::transport::StdioTransport::routeIncomingLine(router, emptyLine, stdoutCapture, &stderrCapture, options);
    REQUIRE_FALSE(emptyAccepted);

    // Clear streams for the next check
    stdoutCapture.str("");
    stderrCapture.str("");

    // Then a valid message should work
    const std::string validMessage = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    const bool validAccepted = mcp::transport::StdioTransport::routeIncomingLine(router, validMessage, stdoutCapture, &stderrCapture, options);
    REQUIRE(validAccepted);
    REQUIRE_FALSE(stdoutCapture.str().empty());

    // Verify the response is valid JSON-RPC
    const auto message = mcp::jsonrpc::parseMessage(stdoutCapture.str());
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message));
  }
}

TEST_CASE("Stdio transport parse error emission keeps stdout MCP-only", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;
  const mcp::transport::StdioAttachOptions options {.allowStderrLogs = true, .emitParseErrors = true};

  // Send an invalid message that will trigger a parse error response
  const std::string invalidJson = "not valid json";
  const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, invalidJson, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(accepted);

  // stderr should have diagnostic info
  REQUIRE(stderrCapture.str().find("parse failed") != std::string::npos);

  // stdout should ONLY contain valid JSON-RPC (the error response)
  REQUIRE_FALSE(stdoutCapture.str().empty());

  // Verify stdout contains exactly one valid JSON-RPC message per line
  std::istringstream stdoutStream(stdoutCapture.str());
  std::string line;
  int lineCount = 0;
  while (std::getline(stdoutStream, line))
  {
    if (line.empty())
    {
      continue;
    }
    ++lineCount;
    // Each non-empty line must be valid JSON-RPC
    REQUIRE_NOTHROW(mcp::jsonrpc::parseMessage(line));
    // And it should be an error response
    const auto message = mcp::jsonrpc::parseMessage(line);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(message));
  }
  REQUIRE(lineCount == 1);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while,cppcoreguidelines-avoid-goto)
TEST_CASE("StdioTransport instance API is deprecated and throws with clear guidance", "[transport][stdio][regression]")
{
  // Regression test: Ensure instance-level StdioTransport API throws with clear guidance
  // instead of silently ignoring options or partially working.
  // See: task-013 - Clarify StdioTransport Instance API

  SECTION("Server options constructor throws with guidance")
  {
    mcp::transport::StdioServerOptions options {.allowStderrLogs = true};

    try
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
      // NOLINTNEXTLINE(hicpp-avoid-goto,cppcoreguidelines-avoid-goto,deprecated-declarations)
      mcp::transport::StdioTransport transport(options);
#pragma GCC diagnostic pop
      FAIL("Expected constructor to throw std::logic_error");
    }
    catch (const std::logic_error &error)
    {
      const std::string message = error.what();
      REQUIRE(message.find("deprecated") != std::string::npos);
      REQUIRE(message.find("throws") != std::string::npos);
      REQUIRE(message.find("StdioTransport::run()") != std::string::npos);
      REQUIRE(message.find("mcp::Client::connectStdio()") != std::string::npos);
    }
  }

  SECTION("Client options constructor throws with guidance")
  {
    mcp::transport::StdioClientOptions options {.executablePath = "/bin/false"};

    try
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
      // NOLINTNEXTLINE(hicpp-avoid-goto,cppcoreguidelines-avoid-goto,deprecated-declarations)
      mcp::transport::StdioTransport transport(options);
#pragma GCC diagnostic pop
      FAIL("Expected constructor to throw std::logic_error");
    }
    catch (const std::logic_error &error)
    {
      const std::string message = error.what();
      REQUIRE(message.find("deprecated") != std::string::npos);
      REQUIRE(message.find("throws") != std::string::npos);
      REQUIRE(message.find("mcp::Client::connectStdio()") != std::string::npos);
    }
  }

  SECTION("Default constructor throws with guidance")
  {
    try
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
      // NOLINTNEXTLINE(hicpp-avoid-goto,cppcoreguidelines-avoid-goto,deprecated-declarations)
      mcp::transport::StdioTransport transport;
#pragma GCC diagnostic pop
      FAIL("Expected constructor to throw std::logic_error");
    }
    catch (const std::logic_error &error)
    {
      const std::string message = error.what();
      REQUIRE(message.find("deprecated") != std::string::npos);
      REQUIRE(message.find("throws") != std::string::npos);
      REQUIRE(message.find("StdioTransport::run()") != std::string::npos);
      REQUIRE(message.find("mcp::Client::connectStdio()") != std::string::npos);
    }
  }

  SECTION("Static utilities remain functional")
  {
    // Verify static methods are NOT deprecated and still work
    mcp::jsonrpc::Router router;
    registerPingHandler(router);

    std::ostringstream stdoutCapture;
    std::ostringstream stderrCapture;
    const mcp::transport::StdioAttachOptions attachOptions {.allowStderrLogs = true, .emitParseErrors = false};

    // Static routeIncomingLine should work without throwing
    const std::string validMessage = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    // NOLINTNEXTLINE(hicpp-avoid-goto,cppcoreguidelines-avoid-goto)
    REQUIRE_NOTHROW(mcp::transport::StdioTransport::routeIncomingLine(router, validMessage, stdoutCapture, &stderrCapture, attachOptions));

    // Verify it actually routed
    REQUIRE_FALSE(stdoutCapture.str().empty());
    const auto message = mcp::jsonrpc::parseMessage(stdoutCapture.str());
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message));
  }
}

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif
