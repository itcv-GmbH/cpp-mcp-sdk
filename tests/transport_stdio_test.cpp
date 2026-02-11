#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/transport/stdio.hpp>

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
