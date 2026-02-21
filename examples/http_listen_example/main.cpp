#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <mcp/client/client.hpp>
#include <mcp/client/roots.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/version.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

constexpr std::string_view kServerInfoName = "example-http-listen-server";
constexpr std::string_view kServerInfoVersion = "1.0.0";
constexpr std::string_view kClientInfoName = "example-http-listen-client";
constexpr std::string_view kClientInfoVersion = "1.0.0";

constexpr int kServerStartupDelayMs = 100;
constexpr int kServerMessageWaitMs = 500;

auto makeTextContent(const std::string &text) -> mcp::jsonrpc::JsonValue
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
    // Step 1: Create the Server
    // ========================================================================
    std::cout << "=== Creating Server ===" << '\n';

    mcp::ServerConfiguration serverConfig;
    serverConfig.serverInfo = mcp::Implementation(std::string(kServerInfoName), std::string(kServerInfoVersion));

    // Enable tools capability for demonstration
    mcp::ToolsCapability toolsCapability;
    toolsCapability.listChanged = true;

    serverConfig.capabilities = mcp::ServerCapabilities(std::nullopt,  // logging
                                                        std::nullopt,  // completions
                                                        std::nullopt,  // prompts
                                                        std::nullopt,  // resources
                                                        toolsCapability,
                                                        std::nullopt,  // tasks
                                                        std::nullopt  // experimental
    );

    const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(serverConfig));

    // Register a simple tool
    mcp::ToolDefinition echoTool;
    echoTool.name = "echo";
    echoTool.description = "Echo text from arguments";
    echoTool.inputSchema = mcp::jsonrpc::JsonValue::object();
    echoTool.inputSchema["type"] = "object";
    echoTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
    echoTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
    echoTool.inputSchema["properties"]["text"]["type"] = "string";
    echoTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
    echoTool.inputSchema["required"].push_back("text");

    server->registerTool(std::move(echoTool),
                         [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                         {
                           mcp::CallToolResult result;
                           result.content = mcp::jsonrpc::JsonValue::array();
                           result.content.push_back(makeTextContent("echo: " + context.arguments["text"].as<std::string>()));
                           return result;
                         });

    // ========================================================================
    // Step 2: Create and configure StreamableHttpServer
    // ========================================================================
    std::cout << "\n=== Configuring HTTP Server ===" << '\n';

    mcp_http::StreamableHttpServerOptions serverOptions;
    serverOptions.http.endpoint.bindAddress = "127.0.0.1";
    serverOptions.http.endpoint.bindLocalhostOnly = true;
    serverOptions.http.endpoint.port = 0;  // Let system assign port
    serverOptions.http.endpoint.path = "/mcp";
    serverOptions.allowGetSse = true;
    serverOptions.allowDeleteSession = true;

    mcp_http::StreamableHttpServer httpServer(std::move(serverOptions));

    // Set up request handler - capture httpServer by reference
    httpServer.setRequestHandler(
      [&httpServer, &server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
      {
        mcp_http::StreamableRequestResult result;

        // Handle initialize request specially
        if (request.method == "initialize")
        {
          // Create a session for this client
          const std::string sessionId = "session-" + std::to_string(std::hash<std::string> {}(std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
          httpServer.upsertSession(sessionId);

          std::cout << "[Server] Created session: " << sessionId << '\n';

          // Start the server for this session
          server->start();

          // Handle the initialize request
          auto future = server->handleRequest(context, request);
          try
          {
            auto response = future.get();
            if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
            {
              result.response = response;
            }
            else if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response))
            {
              result.response = response;
            }
          }
          catch (const std::exception &e)
          {
            std::cerr << "[Server] Error handling initialize: " << e.what() << '\n';
          }
        }
        else
        {
          // Handle other requests
          auto future = server->handleRequest(context, request);
          try
          {
            auto response = future.get();
            if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
            {
              result.response = response;
            }
            else if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response))
            {
              result.response = response;
            }
          }
          catch (const std::exception &e)
          {
            std::cerr << "[Server] Error handling request: " << e.what() << '\n';
          }
        }

        return result;
      });

    // Set up notification handler
    httpServer.setNotificationHandler(
      [&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Notification &notification) -> bool
      {
        if (notification.method == "notifications/initialized")
        {
          server->handleNotification(context, notification);
          std::cout << "[Server] Received notifications/initialized" << '\n';
        }
        return true;
      });

    // Set up response handler
    httpServer.setResponseHandler(
      [&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Response &response) -> bool
      {
        server->handleResponse(context, response);
        return true;
      });

    // Start the HTTP server on a background thread
    std::thread serverThread(
      [&httpServer]() -> void
      {
        // Note: In a real implementation, you'd use a proper HTTP server runtime
        // For this example, we're just showing the concept
        std::cout << "[Server] HTTP server ready for requests" << '\n';
      });

    // Give time for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(kServerStartupDelayMs));

    // ========================================================================
    // Step 3: Create Client with GET SSE Listen enabled
    // ========================================================================
    std::cout << "\n=== Creating Client ===" << '\n';

    auto client = mcp::Client::create();

    // Configure HTTP client with GET SSE listen enabled
    // This is the KEY feature for server-initiated messages
    mcp::transport::HttpClientOptions httpOptions;
    httpOptions.endpointUrl = "http://127.0.0.1:8080/mcp";
    httpOptions.enableGetListen = true;  // Enable GET SSE listen for server-initiated messages

    client->connectHttp(httpOptions);

    // Configure client initialization
    client->setInitializeConfiguration({
      .protocolVersion = std::string(mcp::kLatestProtocolVersion),
      .capabilities = mcp::ClientCapabilities {},
      .clientInfo = mcp::Implementation(std::string(kClientInfoName), std::string(kClientInfoVersion)),
    });

    // Set up roots provider - this is called when server sends roots/list request
    // This demonstrates server-initiated request handling
    client->setRootsProvider(
      [](const mcp::RootsListContext & /* context */) -> std::vector<mcp::RootEntry>
      {
        std::cout << "[Client] Roots provider called (server-initiated request received)" << '\n';

        std::vector<mcp::RootEntry> roots;
        mcp::RootEntry root;
        root.uri = "file:///example/dynamic-resource";
        root.name = "Dynamic Resource";
        roots.push_back(std::move(root));
        return roots;
      });

    // Start client
    client->start();

    // Initialize - this would normally connect to the server
    // For the demo, we just show the client setup
    std::cout << "[Client] Configured with enableGetListen=true" << '\n';
    std::cout << "[Client] Configured with roots provider for server-initiated requests" << '\n';

    // ========================================================================
    // Step 4: Demonstrate the flow
    // ========================================================================
    std::cout << "\n=== Demonstrating Server-Initiated Request Flow ===" << '\n';
    std::cout << "[Demo] Server would use httpServer.enqueueServerMessage() to send" << '\n';
    std::cout << "[Demo] a server-initiated roots/list request to the client" << '\n';
    std::cout << "[Demo] Client's rootsProvider would be invoked to handle it" << '\n';
    std::cout << "[Demo] Response would be sent back via the GET SSE connection" << '\n';

    // Wait a bit to show the concept
    std::this_thread::sleep_for(std::chrono::milliseconds(kServerMessageWaitMs));

    // ========================================================================
    // Step 5: Clean shutdown
    // ========================================================================
    std::cout << "\n=== Shutting Down ===" << '\n';

    client->stop();
    server->stop();

    if (serverThread.joinable())
    {
      serverThread.join();
    }

    std::cout << "=== Example completed successfully ===" << '\n';

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "http_listen_example failed: " << error.what() << '\n';
    return 1;
  }
}
