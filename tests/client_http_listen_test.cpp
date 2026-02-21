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
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/version.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)

namespace
{

namespace mcp_http = mcp::transport::http;

auto makeClientOptions() -> mcp_http::StreamableHttpClientOptions
{
  mcp_http::StreamableHttpClientOptions options;
  options.endpointUrl = "http://localhost/mcp";
  options.defaultRetryMilliseconds = 10;
  options.enableGetListen = true;
  return options;
}

auto makeRootsListRequest(std::int64_t id) -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = "roots/list";
  request.params = mcp::jsonrpc::JsonValue::object();
  return mcp::jsonrpc::Message {request};
}

auto makeInitializeRequest(std::int64_t id) -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = "initialize";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  (*request.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";
  return mcp::jsonrpc::Message {request};
}

auto makeNotification(std::string method) -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::Notification notification;
  notification.method = std::move(method);
  return mcp::jsonrpc::Message {notification};
}

}  // namespace

// Test that demonstrates server-initiated JSON-RPC requests delivered over GET SSE stream
// Complete round-trip: server enqueues roots/list -> client polls -> receives request -> sends response
TEST_CASE("StreamableHttpClient receives and responds to server-initiated roots/list over GET SSE", "[client][http][listen]")
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

  // Create StreamableHttpClient with GET listen enabled
  mcp_http::StreamableHttpClientOptions clientOptions = makeClientOptions();
  mcp_http::StreamableHttpClient httpClient(std::move(clientOptions), std::move(requestExecutor));

  // Step 1: Send initialize - server returns useSse=true to enable GET SSE listen
  const auto initResult = httpClient.send(makeInitializeRequest(1));
  REQUIRE(initResult.statusCode == 200);
  REQUIRE(initResult.response.has_value());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(*initResult.response));

  // Step 2: Send notifications/initialized to complete the handshake
  const auto notifResult = httpClient.send(makeNotification("notifications/initialized"));
  REQUIRE(notifResult.statusCode == 202);

  // Step 3: Open the GET SSE listen stream (this is the GET listen loop for server-initiated messages)
  const auto listenOpenResult = httpClient.openListenStream();
  REQUIRE(listenOpenResult.statusCode == 200);
  REQUIRE(listenOpenResult.streamOpen);

  // Step 4: Enqueue a roots/list request from the server (server-initiated JSON-RPC request)
  const bool enqueueSuccess = server.enqueueServerMessage(makeRootsListRequest(100));
  REQUIRE(enqueueSuccess);

  // Step 5: Poll the listen stream to receive the server-initiated request
  const auto pollResult = httpClient.pollListenStream();
  REQUIRE(pollResult.statusCode == 200);
  REQUIRE(pollResult.streamOpen);
  REQUIRE(pollResult.messages.size() == 1);

  // Verify it's a roots/list request from the server
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(pollResult.messages.front()));
  const auto &rootsRequest = std::get<mcp::jsonrpc::Request>(pollResult.messages.front());
  REQUIRE(rootsRequest.method == "roots/list");
  REQUIRE(rootsRequest.id == mcp::jsonrpc::RequestId {std::int64_t {100}});

  // Step 6: Send response back to server (completing the round-trip)
  mcp::jsonrpc::SuccessResponse manualResponse;
  manualResponse.id = rootsRequest.id;
  manualResponse.result = mcp::jsonrpc::JsonValue::object();
  manualResponse.result["roots"] = mcp::jsonrpc::JsonValue::array();

  const auto responseResult = httpClient.send(mcp::jsonrpc::Message {manualResponse});
  REQUIRE(responseResult.statusCode == 202);

  // Step 7: Verify the server received the response with correct structure
  {
    const std::scoped_lock lock(responseMutex);
    REQUIRE(serverReceivedResponses.size() == 1);

    const auto &response = serverReceivedResponses.front();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

    const auto &successResponse = std::get<mcp::jsonrpc::SuccessResponse>(response);
    REQUIRE(successResponse.result.contains("roots"));
    REQUIRE(successResponse.result["roots"].is_array());
    REQUIRE(successResponse.id == mcp::jsonrpc::RequestId {std::int64_t {100}});
  }
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)
