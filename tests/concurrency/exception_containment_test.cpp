#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/sdk/error_reporter.hpp>
#include <mcp/transport/http.hpp>

namespace
{

constexpr auto kWaitTimeout = std::chrono::seconds {2};

auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

}  // namespace

TEST_CASE("Router completion worker contains handler exceptions and continues routing", "[concurrency][exceptions][router]")
{
  std::mutex errorsMutex;
  std::vector<mcp::ErrorEvent> errors;
  std::promise<void> errorReportedPromise;
  std::future<void> errorReportedFuture = errorReportedPromise.get_future();
  std::once_flag errorReportedOnce;

  mcp::jsonrpc::RouterOptions options;
  options.errorReporter = [&errorsMutex, &errors, &errorReportedPromise, &errorReportedOnce](const mcp::ErrorEvent &event) -> void
  {
    {
      const std::scoped_lock lock(errorsMutex);
      errors.push_back(event);
    }

    if (std::string(event.message()).find("Injected async handler failure") != std::string::npos)
    {
      std::call_once(errorReportedOnce, [&errorReportedPromise]() -> void { errorReportedPromise.set_value(); });
    }
  };

  mcp::jsonrpc::Router router(options);
  router.registerRequestHandler("failing/op",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> std::future<mcp::jsonrpc::Response>
                                { return std::async(std::launch::async, []() -> mcp::jsonrpc::Response { throw std::runtime_error("Injected async handler failure"); }); });

  mcp::jsonrpc::Request failingRequest;
  failingRequest.id = std::int64_t {1};
  failingRequest.method = "failing/op";
  failingRequest.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> failingResponseFuture = router.dispatchRequest(mcp::jsonrpc::RequestContext {}, failingRequest);
  REQUIRE(failingResponseFuture.wait_for(kWaitTimeout) == std::future_status::ready);

  const mcp::jsonrpc::Response failingResponse = failingResponseFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(failingResponse));
  const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(failingResponse);
  REQUIRE(errorResponse.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));
  REQUIRE(errorResponse.error.message == "Request handler threw an exception.");

  REQUIRE(errorReportedFuture.wait_for(kWaitTimeout) == std::future_status::ready);

  {
    const std::scoped_lock lock(errorsMutex);
    REQUIRE_FALSE(errors.empty());

    bool sawExpectedComponent = false;
    for (const auto &event : errors)
    {
      if (event.component() == "Router::dispatchRequest" && std::string(event.message()).find("Injected async handler failure") != std::string::npos)
      {
        sawExpectedComponent = true;
        break;
      }
    }

    REQUIRE(sawExpectedComponent);
  }

  router.registerRequestHandler("healthy/op",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse success;
                                  success.id = request.id;
                                  success.result = mcp::jsonrpc::JsonValue::object();
                                  success.result["healthy"] = true;
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {std::move(success)});
                                });

  mcp::jsonrpc::Request healthyRequest;
  healthyRequest.id = std::int64_t {2};
  healthyRequest.method = "healthy/op";
  healthyRequest.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> healthyResponseFuture = router.dispatchRequest(mcp::jsonrpc::RequestContext {}, healthyRequest);
  REQUIRE(healthyResponseFuture.wait_for(kWaitTimeout) == std::future_status::ready);

  const mcp::jsonrpc::Response healthyResponse = healthyResponseFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(healthyResponse));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(healthyResponse).result["healthy"].as<bool>());
}

TEST_CASE("HttpServerRuntime thread boundary reports failure and supports restart", "[concurrency][exceptions][http_runtime]")
{
  std::promise<void> errorReportedPromise;
  std::future<void> errorReportedFuture = errorReportedPromise.get_future();
  std::once_flag errorReportedOnce;

  mcp::transport::HttpServerOptions serverOptions;
  serverOptions.endpoint.bindAddress = "127.0.0.1";
  serverOptions.endpoint.bindLocalhostOnly = true;
  serverOptions.endpoint.port = 0;
  serverOptions.errorReporter = [&errorReportedPromise, &errorReportedOnce](const mcp::ErrorEvent &event) -> void
  {
    if (event.component() == "HttpServerRuntime" && std::string(event.message()).find("Injected runtime handler failure") != std::string::npos)
    {
      std::call_once(errorReportedOnce, [&errorReportedPromise]() -> void { errorReportedPromise.set_value(); });
    }
  };

  mcp::transport::HttpServerRuntime runtime(serverOptions);

  bool shouldThrow = true;
  runtime.setRequestHandler(
    [&shouldThrow](const mcp::transport::http::ServerRequest &) -> mcp::transport::http::ServerResponse
    {
      if (shouldThrow)
      {
        shouldThrow = false;
        throw std::runtime_error("Injected runtime handler failure");
      }

      mcp::transport::http::ServerResponse response;
      response.statusCode = 200;
      response.body = "ok";
      return response;
    });

  runtime.start();
  const std::uint16_t initialPort = runtime.localPort();
  REQUIRE(initialPort != 0);

  mcp::transport::HttpClientOptions failingClientOptions;
  failingClientOptions.endpointUrl = "http://127.0.0.1:" + std::to_string(initialPort) + "/mcp";
  mcp::transport::HttpClientRuntime failingClient(failingClientOptions);

  mcp::transport::http::ServerRequest request;
  request.method = mcp::transport::http::ServerRequestMethod::kPost;
  request.path = "/mcp";
  request.body = "{}";

  REQUIRE_THROWS(failingClient.execute(request));
  REQUIRE(errorReportedFuture.wait_for(kWaitTimeout) == std::future_status::ready);

  const auto shutdownDeadline = std::chrono::steady_clock::now() + kWaitTimeout;
  while (runtime.isRunning() && std::chrono::steady_clock::now() < shutdownDeadline)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds {10});
  }
  REQUIRE_FALSE(runtime.isRunning());

  REQUIRE_NOTHROW(runtime.stop());
  REQUIRE_NOTHROW(runtime.start());
  REQUIRE(runtime.isRunning());

  mcp::transport::HttpClientOptions healthyClientOptions;
  healthyClientOptions.endpointUrl = "http://127.0.0.1:" + std::to_string(runtime.localPort()) + "/mcp";
  mcp::transport::HttpClientRuntime healthyClient(healthyClientOptions);
  const mcp::transport::http::ServerResponse response = healthyClient.execute(request);
  REQUIRE(response.statusCode == 200);
  REQUIRE(response.body == "ok");

  runtime.stop();
}
