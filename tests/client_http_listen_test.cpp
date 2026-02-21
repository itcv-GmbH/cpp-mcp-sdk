#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/client/client.hpp>
#include <mcp/client/roots.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/version.hpp>

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
    [&responseMutex, &serverReceivedResponses](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Response &response) -> bool
    {
      const std::scoped_lock lock(responseMutex);
      serverReceivedResponses.push_back(response);
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

  // Give the client a moment to set up the listen stream after notifications/initialized
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Step 7: Enqueue a roots/list request from the server (server-initiated JSON-RPC request)
  mcp::jsonrpc::Request rootsListRequest;
  rootsListRequest.id = mcp::jsonrpc::RequestId {std::int64_t {100}};
  rootsListRequest.method = "roots/list";
  rootsListRequest.params = mcp::jsonrpc::JsonValue::object();

  const bool enqueueSuccess = server.enqueueServerMessage(mcp::jsonrpc::Message {rootsListRequest});
  REQUIRE(enqueueSuccess);

  // Step 8: Poll the listen stream to receive the server-initiated request
  // Note: The client automatically handles the roots/list request by:
  //   1. Receiving the request from the server
  //   2. Invoking the roots provider
  //   3. Sending the response back to the server
  // We need to poll the transport to trigger the listen cycle
  // Since we're using in-process transport, we need to manually trigger this

  // Wait a bit for the client to process the request and send response
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Step 9: Verify the server received the response with correct structure
  {
    const std::scoped_lock lock(responseMutex);
    REQUIRE(serverReceivedResponses.size() == 1);

    const auto &response = serverReceivedResponses.front();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

    const auto &successResponse = std::get<mcp::jsonrpc::SuccessResponse>(response);
    REQUIRE(successResponse.result.contains("roots"));
    REQUIRE(successResponse.result["roots"].is_array());
    REQUIRE(successResponse.result["roots"].size() == 1);
    REQUIRE(successResponse.result["roots"][0]["uri"] == "file:///test");
    REQUIRE(successResponse.result["roots"][0]["name"] == "Test Root");
    REQUIRE(successResponse.id == mcp::jsonrpc::RequestId {std::int64_t {100}});
  }

  // Clean up
  client->stop();
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)
