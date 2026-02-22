#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/client/client.hpp>
#include <mcp/client/roots.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/streamable_http_client_transport.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)

namespace
{

namespace mcp_http = mcp::transport::http;

auto makeHttpClientOptions() -> mcp_http::StreamableHttpClientOptions
{
  mcp_http::StreamableHttpClientOptions options;
  options.endpointUrl = "http://localhost/mcp";
  options.defaultRetryMilliseconds = 10;
  options.enableGetListen = true;
  return options;
}

}  // namespace

// Test that demonstrates server-initiated JSON-RPC requests delivered over GET SSE stream
// Complete round-trip: server enqueues roots/list -> client polls -> roots provider invoked -> response sent back
TEST_CASE("mcp::Client receives and responds to server-initiated roots/list over GET SSE", "[client][http][listen]")
{
  // Track server-side state for responses
  std::mutex responseMutex;
  std::vector<mcp::jsonrpc::Response> serverReceivedResponses;
  std::condition_variable responseCv;
  bool rootsListResponseReceived = false;
  const mcp::jsonrpc::RequestId expectedRootsRequestId {std::int64_t {100}};

  // Create in-process server
  mcp_http::StreamableHttpServer server;

  // Set up request handler for initialize (returns useSse=true to enable GET listen)
  server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;

      if (request.method == "initialize")
      {
        mcp::jsonrpc::SuccessResponse response;
        response.id = request.id;
        response.result = mcp::jsonrpc::JsonValue::object();
        response.result["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);

        // Include roots capability to indicate server supports server-initiated requests
        mcp::jsonrpc::JsonValue serverCapabilities = mcp::jsonrpc::JsonValue::object();
        serverCapabilities["roots"] = mcp::jsonrpc::JsonValue::object();
        response.result["capabilities"] = serverCapabilities;

        response.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
        response.result["serverInfo"]["name"] = "test-server";
        response.result["serverInfo"]["version"] = "1.0.0";

        result.response = mcp::jsonrpc::Response {std::move(response)};
        result.useSse = true;
        result.closeSseConnection = false;

        return result;
      }

      // For any other request, return method not found
      mcp::jsonrpc::ErrorResponse error;
      error.id = request.id;
      error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound);
      error.error.message = "Method not found";
      result.response = mcp::jsonrpc::Response {std::move(error)};

      return result;
    });

  // Set up notification handler
  server.setNotificationHandler([](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &) -> bool { return true; });

  // Set up response handler to capture responses sent back by the client
  server.setResponseHandler(
    [&responseMutex, &serverReceivedResponses, &rootsListResponseReceived, &responseCv, &expectedRootsRequestId](const mcp::jsonrpc::RequestContext &,
                                                                                                                 const mcp::jsonrpc::Response &response) -> bool
    {
      const std::scoped_lock lock(responseMutex);
      serverReceivedResponses.push_back(response);
      if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response) && std::get<mcp::jsonrpc::SuccessResponse>(response).id == expectedRootsRequestId)
      {
        rootsListResponseReceived = true;
      }
      else if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response) && std::get<mcp::jsonrpc::ErrorResponse>(response).id
               && std::get<mcp::jsonrpc::ErrorResponse>(response).id == expectedRootsRequestId)
      {
        rootsListResponseReceived = true;
      }

      responseCv.notify_all();
      return true;
    });

  // Create request executor that delegates to in-process server
  auto requestExecutor = [&server](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return server.handleRequest(request); };

  // Step 1: Create mcp::Client instance
  auto client = mcp::Client::create();

  // Step 2: Configure roots provider
  client->setRootsProvider(
    [](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry>
    {
      mcp::RootEntry entry;
      entry.uri = "file:///test";
      entry.name = "Test Root";
      return {entry};
    });

  // Step 3: Connect using Client::connectHttp with RequestExecutor
  mcp_http::StreamableHttpClientOptions clientOptions = makeHttpClientOptions();
  client->connectHttp(std::move(clientOptions), std::move(requestExecutor));

  // Step 4: Start the client
  client->start();

  // Step 5: Run initialization
  // Set up client capabilities to declare roots support - required for handling roots/list requests
  mcp::RootsCapability rootsCapability;
  rootsCapability.listChanged = true;

  mcp::ClientInitializeConfiguration initConfig;
  initConfig.clientInfo = mcp::Implementation {"test-client", "1.0.0"};
  initConfig.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(initConfig);

  auto initFuture = client->initialize();
  mcp::jsonrpc::Response initResponse = initFuture.get();

  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initResponse));

  const auto &initSuccess = std::get<mcp::jsonrpc::SuccessResponse>(initResponse);
  REQUIRE(initSuccess.result.contains("capabilities"));
  REQUIRE(initSuccess.result["capabilities"].is_object());
  REQUIRE(initSuccess.result["capabilities"].contains("roots"));

  // Note: The client automatically sends notifications/initialized after initialize completes
  // This is required before the server can send server-initiated requests

  // Step 7: Enqueue a roots/list request from the server (server-initiated JSON-RPC request)
  mcp::jsonrpc::Request rootsListRequest;
  rootsListRequest.id = expectedRootsRequestId;
  rootsListRequest.method = "roots/list";
  rootsListRequest.params = mcp::jsonrpc::JsonValue::object();

  const bool enqueueSuccess = server.enqueueServerMessage(mcp::jsonrpc::Message {rootsListRequest});
  REQUIRE(enqueueSuccess);

  // Step 8: Wait for the client to process the server-initiated request and post a response
  {
    std::unique_lock lock(responseMutex);
    REQUIRE(responseCv.wait_for(lock, std::chrono::seconds {5}, [&rootsListResponseReceived] { return rootsListResponseReceived; }) == true);
  }

  // Step 9: Verify the server received the response with correct structure
  {
    const std::scoped_lock lock(responseMutex);
    REQUIRE(serverReceivedResponses.size() >= 1);

    const auto responseIt =
      std::find_if(serverReceivedResponses.begin(),
                   serverReceivedResponses.end(),
                   [&expectedRootsRequestId](const auto &candidate)
                   {
                     if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(candidate) && std::get<mcp::jsonrpc::SuccessResponse>(candidate).id == expectedRootsRequestId)
                     {
                       return true;
                     }

                     if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(candidate) && std::get<mcp::jsonrpc::ErrorResponse>(candidate).id
                         && std::get<mcp::jsonrpc::ErrorResponse>(candidate).id == expectedRootsRequestId)
                     {
                       return true;
                     }

                     return false;
                   });

    REQUIRE(responseIt != serverReceivedResponses.end());
    const auto &response = *responseIt;
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

    const auto &successResponse = std::get<mcp::jsonrpc::SuccessResponse>(response);
    REQUIRE(successResponse.result.contains("roots"));
    REQUIRE(successResponse.result["roots"].is_array());
    REQUIRE(successResponse.result["roots"].size() == 1);
    REQUIRE(successResponse.result["roots"][0]["uri"] == "file:///test");
    REQUIRE(successResponse.result["roots"][0]["name"] == "Test Root");
    REQUIRE(successResponse.id == expectedRootsRequestId);
  }

  // Clean up
  client->stop();
}

TEST_CASE("Streamable HTTP transport supports repeated start-stop listen cycles", "[client][http][listen][lifecycle]")
{
  mcp_http::StreamableHttpClientOptions options = makeHttpClientOptions();
  options.enableGetListen = true;

  auto transport = mcp::transport::makeStreamableHttpClientTransport(
    std::move(options),
    [](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
    {
      mcp_http::ServerResponse response;
      if (request.method == mcp_http::ServerRequestMethod::kPost)
      {
        response.statusCode = 202;
        return response;
      }

      response.statusCode = 405;
      return response;
    },
    [](const mcp::jsonrpc::Message &) -> void {});

  constexpr auto kStopTimeout = std::chrono::milliseconds {500};
  constexpr std::size_t kCycles = 3;

  for (std::size_t cycle = 0; cycle < kCycles; ++cycle)
  {
    transport->start();

    mcp::jsonrpc::Notification initialized;
    initialized.method = "notifications/initialized";
    transport->send(mcp::jsonrpc::Message {initialized});

    auto stopFuture = std::async(std::launch::async, [&transport]() -> void { transport->stop(); });
    REQUIRE(stopFuture.wait_for(kStopTimeout) == std::future_status::ready);
    stopFuture.get();
  }
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)
