#include <future>
#include <iostream>
#include <string>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/transport/stdio.hpp>

namespace
{

auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

}  // namespace

auto main() -> int
{
  std::cerr << "helper-server-started" << '\n';
  std::cerr.flush();

  mcp::jsonrpc::Router router;
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse response;
                                  response.id = request.id;
                                  response.result = mcp::jsonrpc::JsonValue::object();
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {response});
                                });

  router.registerRequestHandler("initialize",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse response;
                                  response.id = request.id;
                                  response.result = mcp::jsonrpc::JsonValue::object();
                                  response.result["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
                                  response.result["capabilities"] = mcp::jsonrpc::JsonValue::object();
                                  response.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
                                  response.result["serverInfo"]["name"] = "stdio-helper";
                                  response.result["serverInfo"]["version"] = "1.0.0";
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {response});
                                });

  mcp::transport::StdioTransport::run(router, mcp::transport::StdioServerOptions {.allowStderrLogs = true});
  return 0;
}
