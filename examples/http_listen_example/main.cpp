#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/client/client.hpp>
#include <mcp/client/roots.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/transport/http.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

constexpr std::string_view kServerInfoName = "example-http-listen-server";
constexpr std::string_view kServerInfoVersion = "1.0.0";
constexpr std::string_view kClientInfoName = "example-http-listen-client";
constexpr std::string_view kClientInfoVersion = "1.0.0";

constexpr int kServerStartupDelayMs = 100;
constexpr int kResponseWaitMs = 1000;

constexpr std::int64_t kRootsListRequestId = 100;

auto makeTextContent(const std::string &text) -> mcp::jsonrpc::JsonValue  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = text;
  return content;
}

}  // namespace

auto main() -> int
{
  try
  {
    // ========================================================================
    // Step 1: Create an in-process StreamableHttpServer
    // ========================================================================
    std::cout << "=== Creating In-Process HTTP Server ===" << '\n';

    mcp_http::StreamableHttpServer server;

    // Track server-side state for responses
    std::mutex responseMutex;
    std::vector<mcp::jsonrpc::Response> serverReceivedResponses;

    // Set up request handler for initialize
    // Returns useSse=true to enable GET SSE listen on the client
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
          // This is REQUIRED for the client to enable GET SSE listen
          mcp::jsonrpc::JsonValue serverCapabilities = mcp::jsonrpc::JsonValue::object();
          serverCapabilities["roots"] = mcp::jsonrpc::JsonValue::object();
          response.result["capabilities"] = serverCapabilities;

          response.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
          response.result["serverInfo"]["name"] = "example-http-listen-server";
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
    server.setNotificationHandler(
      [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &notification) -> bool
      {
        if (notification.method == "notifications/initialized")
        {
          std::cout << "[Server] Received notifications/initialized from client" << '\n';
        }
        return true;
      });

    // Set up response handler to capture responses sent back by the client
    server.setResponseHandler(
      [&responseMutex, &serverReceivedResponses](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Response &response) -> bool
      {
        const std::scoped_lock lock(responseMutex);
        serverReceivedResponses.push_back(response);
        std::cout << "[Server] Received response from client" << '\n';
        return true;
      });

    // Create request executor that delegates to in-process server
    auto requestExecutor = [&server](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return server.handleRequest(request); };

    // ========================================================================
    // Step 2: Create Client with GET SSE Listen enabled
    // ========================================================================
    std::cout << "\n=== Creating Client ===" << '\n';

    auto client = mcp::Client::create();

    // Configure roots provider - this is called when server sends roots/list request
    // This demonstrates server-initiated request handling
    client->setRootsProvider(
      [](const mcp::RootsListContext & /* context */) -> std::vector<mcp::RootEntry>
      {
        std::cout << "[Client] Roots provider invoked (server-initiated request received)" << '\n';

        std::vector<mcp::RootEntry> roots;
        mcp::RootEntry root;
        root.uri = "file:///example/dynamic-resource";
        root.name = "Dynamic Resource";
        roots.push_back(std::move(root));
        return roots;
      });

    // Configure HTTP client with GET SSE listen enabled
    // This is the KEY feature for server-initiated messages
    mcp_http::StreamableHttpClientOptions httpOptions;
    httpOptions.endpointUrl = "http://localhost/mcp";
    httpOptions.enableGetListen = true;  // Enable GET SSE listen for server-initiated messages

    client->connectHttp(std::move(httpOptions), std::move(requestExecutor));

    // Configure client initialization
    // Include roots capability to tell server we support server-initiated roots/list
    mcp::RootsCapability rootsCapability;
    rootsCapability.listChanged = true;

    client->setInitializeConfiguration({
      .protocolVersion = std::string(mcp::kLatestProtocolVersion),
      .capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt),
      .clientInfo = mcp::Implementation(std::string(kClientInfoName), std::string(kClientInfoVersion)),
    });

    // Start client
    client->start();

    // Initialize - this triggers the handshake and sets up GET SSE listen
    std::cout << "[Client] Initializing connection..." << '\n';
    auto initFuture = client->initialize();
    const mcp::jsonrpc::Response initResponse = initFuture.get();

    if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initResponse))
    {
      std::cout << "[Client] Initialization successful" << '\n';
    }
    else
    {
      std::cerr << "[Client] Initialization failed" << '\n';
      return 1;
    }

    // Give the client a moment to set up the listen stream after notifications/initialized
    std::this_thread::sleep_for(std::chrono::milliseconds(kServerStartupDelayMs));

    // ========================================================================
    // Step 3: Enqueue a server-initiated roots/list request
    // ========================================================================
    std::cout << "\n=== Sending Server-Initiated Request ===" << '\n';

    mcp::jsonrpc::Request rootsListRequest;
    rootsListRequest.id = mcp::jsonrpc::RequestId {kRootsListRequestId};
    rootsListRequest.method = "roots/list";
    rootsListRequest.params = mcp::jsonrpc::JsonValue::object();

    std::cout << "[Server] Enqueueing roots/list request..." << '\n';
    const bool enqueueSuccess = server.enqueueServerMessage(mcp::jsonrpc::Message {rootsListRequest});
    std::cout << "[Server] Enqueue result: " << (enqueueSuccess ? "success" : "failed") << '\n';

    // Wait for the client to process the request and send response
    std::this_thread::sleep_for(std::chrono::milliseconds(kResponseWaitMs));

    // ========================================================================
    // Step 4: Verify the server received the response
    // ========================================================================
    std::cout << "\n=== Verifying Response ===" << '\n';

    {
      const std::scoped_lock lock(responseMutex);

      if (serverReceivedResponses.empty())
      {
        std::cerr << "[Server] No response received from client!" << '\n';
        return 1;
      }

      const auto &response = serverReceivedResponses.front();
      if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
      {
        const auto &successResponse = std::get<mcp::jsonrpc::SuccessResponse>(response);
        std::cout << "[Server] Received success response" << '\n';

        if (successResponse.result.contains("roots") && successResponse.result["roots"].is_array())
        {
          const auto &rootsArray = successResponse.result["roots"];
          std::cout << "[Server] Response contains " << rootsArray.size() << " root(s)" << '\n';
          for (std::size_t i = 0; i < rootsArray.size(); ++i)
          {
            std::cout << "[Server]   - uri: " << rootsArray[i]["uri"].as<std::string>() << '\n';
            std::cout << "[Server]     name: " << rootsArray[i]["name"].as<std::string>() << '\n';
          }
        }
      }
      else
      {
        std::cerr << "[Server] Received error response!" << '\n';
        return 1;
      }
    }

    // ========================================================================
    // Step 5: Clean shutdown
    // ========================================================================
    std::cout << "\n=== Shutting Down ===" << '\n';

    client->stop();

    std::cout << "=== Example completed successfully ===" << '\n';
    std::cout << "\nServer-initiated message flow demonstrated:" << '\n';
    std::cout << "  1. Server returned useSse=true in initialize response" << '\n';
    std::cout << "  2. Client enabled GET SSE listen" << '\n';
    std::cout << "  3. Server called enqueueServerMessage(roots/list)" << '\n';
    std::cout << "  4. Client received request via GET SSE" << '\n';
    std::cout << "  5. Client invoked roots provider callback" << '\n';
    std::cout << "  6. Client sent response back to server" << '\n';
    std::cout << "  7. Server received and validated response" << '\n';

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "http_listen_example failed: " << error.what() << '\n';
    return 1;
  }
}
