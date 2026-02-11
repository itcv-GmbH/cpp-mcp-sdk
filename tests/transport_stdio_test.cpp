#include <future>
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
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {response});
                                });
}

}  // namespace

TEST_CASE("Stdio transport rejects framed lines with embedded newlines", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::ostringstream stdoutCapture;
  std::ostringstream stderrCapture;

  mcp::transport::StdioAttachOptions options;
  options.allowStderrLogs = true;
  options.emitParseErrors = true;

  const std::string malformedFrame = std::string(R"({"jsonrpc":"2.0","id":1,"method":"ping"})") + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"ping"})";

  const bool accepted = mcp::transport::StdioTransport::routeIncomingLine(router, malformedFrame, stdoutCapture, &stderrCapture, options);
  REQUIRE_FALSE(accepted);
  REQUIRE(stdoutCapture.str().empty());
}

TEST_CASE("Stdio transport output stream remains MCP JSON-RPC only", "[transport][stdio]")
{
  mcp::jsonrpc::Router router;
  registerPingHandler(router);

  std::stringstream simulatedServerStdout;
  simulatedServerStdout << R"({"jsonrpc":"2.0","id":41,"method":"ping"})" << '\n' << "not-json" << '\n';

  std::stringstream simulatedServerStdin;

  mcp::transport::StdioAttachOptions options;
  options.allowStderrLogs = true;
  options.emitParseErrors = true;

  mcp::transport::StdioTransport::attach(router, simulatedServerStdout, simulatedServerStdin, options);

  std::string outputLine;
  bool sawAtLeastOneMessage = false;
  while (std::getline(simulatedServerStdin, outputLine))
  {
    if (outputLine.empty())
    {
      continue;
    }

    sawAtLeastOneMessage = true;
    REQUIRE_NOTHROW(mcp::jsonrpc::parseMessage(outputLine));
  }

  REQUIRE(sawAtLeastOneMessage);
}
