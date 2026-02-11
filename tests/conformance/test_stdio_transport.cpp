#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/transport/stdio.hpp>

namespace
{

auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

auto registerPingHandler(mcp::jsonrpc::Router &router) -> void
{
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse response;
                                  response.id = request.id;
                                  response.result = mcp::jsonrpc::JsonValue::object();
                                  response.result["ok"] = true;
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

}  // namespace

TEST_CASE("stdio transport enforces newline framing", "[conformance][transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;

  const mcp::transport::StdioAttachOptions options {
    .allowStderrLogs = true,
    .emitParseErrors = false,
  };

  const std::string validFrame = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
  REQUIRE(mcp::transport::StdioTransport::routeIncomingLine(router, validFrame, stdoutCapture, &stderrCapture, options));

  const std::size_t acceptedOutputSize = stdoutCapture.str().size();

  const std::string embeddedNewlineFrame = std::string(R"({"jsonrpc":"2.0","id":2,"method":"pi")") + "\n" + R"(ng"})";
  REQUIRE_FALSE(mcp::transport::StdioTransport::routeIncomingLine(router, embeddedNewlineFrame, stdoutCapture, &stderrCapture, options));
  REQUIRE(stdoutCapture.str().size() == acceptedOutputSize);

  std::istringstream framedOutput(stdoutCapture.str());
  std::string outputLine;
  REQUIRE(std::getline(framedOutput, outputLine));
  REQUIRE_NOTHROW(mcp::jsonrpc::parseMessage(outputLine));
  REQUIRE(outputLine.find('\r') == std::string::npos);
}

TEST_CASE("stdio transport keeps stdout MCP-only", "[conformance][transport][stdio]")
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
  bool sawStdoutMessage = false;

  while (std::getline(stdoutStream, outputLine))
  {
    if (outputLine.empty())
    {
      continue;
    }

    sawStdoutMessage = true;
    REQUIRE_NOTHROW(mcp::jsonrpc::parseMessage(outputLine));
    REQUIRE(outputLine.find("stdio transport") == std::string::npos);
  }

  REQUIRE(sawStdoutMessage);
  REQUIRE(stderrCapture.str().find("stdio transport parse failed") != std::string::npos);
}
