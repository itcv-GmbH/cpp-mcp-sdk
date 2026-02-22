#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/client/client.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/http.hpp>

namespace
{

constexpr std::size_t kLifecycleCycles = 4;
constexpr auto kStopTimeout = std::chrono::seconds {2};

}  // namespace

TEST_CASE("HttpServerRuntime start/stop remains idempotent across repeated cycles", "[concurrency][lifecycle][http_runtime]")
{
  mcp::transport::HttpServerOptions serverOptions;
  serverOptions.endpoint.path = "/mcp";
  serverOptions.endpoint.bindAddress = "127.0.0.1";
  serverOptions.endpoint.bindLocalhostOnly = true;
  serverOptions.endpoint.port = 0;

  std::size_t requestCount = 0;
  mcp::transport::HttpServerRuntime runtime(serverOptions);
  runtime.setRequestHandler(
    [&requestCount](const mcp::transport::http::ServerRequest &) -> mcp::transport::http::ServerResponse
    {
      ++requestCount;
      mcp::transport::http::ServerResponse response;
      response.statusCode = 200;
      response.body = "ok";
      return response;
    });

  for (std::size_t cycle = 0; cycle < kLifecycleCycles; ++cycle)
  {
    REQUIRE_NOTHROW(runtime.start());
    REQUIRE(runtime.isRunning());

    const std::uint16_t runningPort = runtime.localPort();
    REQUIRE(runningPort != 0);

    REQUIRE_NOTHROW(runtime.start());
    REQUIRE(runtime.isRunning());
    REQUIRE(runtime.localPort() == runningPort);

    mcp::transport::HttpClientOptions clientOptions;
    clientOptions.endpointUrl = "http://127.0.0.1:" + std::to_string(runningPort) + "/mcp";
    mcp::transport::HttpClientRuntime client(clientOptions);

    mcp::transport::http::ServerRequest request;
    request.method = mcp::transport::http::ServerRequestMethod::kPost;
    request.path = "/mcp";
    request.body = "{}";

    const mcp::transport::http::ServerResponse response = client.execute(request);
    REQUIRE(response.statusCode == 200);
    REQUIRE(response.body == "ok");

    REQUIRE_NOTHROW(runtime.stop());
    REQUIRE_FALSE(runtime.isRunning());
    REQUIRE_NOTHROW(runtime.stop());
  }

  REQUIRE(requestCount == kLifecycleCycles);
}

#ifdef MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH
TEST_CASE("Client stdio subprocess transport supports repeated start/stop cycles", "[concurrency][lifecycle][stdio_subprocess]")
{
  for (std::size_t cycle = 0; cycle < kLifecycleCycles; ++cycle)
  {
    auto client = mcp::Client::create();

    mcp::transport::StdioClientOptions options;
    options.executablePath = MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH;
    client->connectStdio(options);
    client->start();

    const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

    const mcp::jsonrpc::Response pingResponse = client->sendRequest("ping", mcp::jsonrpc::JsonValue::object()).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(pingResponse));

    std::future<void> stopFuture = std::async(std::launch::async, [&client]() -> void { client->stop(); });
    REQUIRE(stopFuture.wait_for(kStopTimeout) == std::future_status::ready);
    REQUIRE_NOTHROW(stopFuture.get());

    REQUIRE_NOTHROW(client->stop());
  }
}
#endif
