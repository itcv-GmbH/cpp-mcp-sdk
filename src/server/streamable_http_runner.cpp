#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/transport/http.hpp>

namespace
{

constexpr std::string_view kName = "StreamableHttpServerRunner";  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

}  // namespace

namespace mcp
{

namespace detail
{

// No-op helper for suppressing exceptions in destructors
inline auto suppressException() noexcept -> void {}  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

}  // namespace detail

struct StreamableHttpServerRunner::Impl
{
  explicit Impl(ServerFactory serverFactoryIn, StreamableHttpServerRunnerOptions optionsIn)
    : serverFactory(std::move(serverFactoryIn))
    , options(std::move(optionsIn))
    , streamableServer(options.transportOptions)
    , runtime(options.transportOptions.http)
  {
  }

  ServerFactory serverFactory;
  StreamableHttpServerRunnerOptions options;
  transport::http::StreamableHttpServer streamableServer;
  transport::HttpServerRuntime runtime;
  std::atomic<bool> running {false};

  // Per-session server instances (used when requireSessionId == true)
  std::map<std::string, std::shared_ptr<Server>> sessionServers;

  // Single server instance (used when requireSessionId == false)
  std::shared_ptr<Server> singleServer;

  // Fixed key for single-server mode
  static constexpr std::string_view kSingleServerKey = "_single_server_";

  auto getOrCreateServer(const std::optional<std::string> &sessionId) -> std::shared_ptr<Server>
  {
    const bool requireSessionId = options.transportOptions.http.requireSessionId;

    if (requireSessionId)
    {
      if (!sessionId.has_value())
      {
        throw std::runtime_error("Session ID is required but not provided");
      }

      // Look up existing server for this session
      const auto it = sessionServers.find(*sessionId);
      if (it != sessionServers.end())
      {
        return it->second;
      }

      // Create new server for this session
      std::shared_ptr<Server> newServer = serverFactory();
      sessionServers[*sessionId] = newServer;
      return newServer;
    }
    else
    {
      // Use single server instance
      if (!singleServer)
      {
        singleServer = serverFactory();
      }
      return singleServer;
    }
  }

  auto removeServer(const std::string &sessionId) -> void
  {
    const auto it = sessionServers.find(sessionId);
    if (it != sessionServers.end())
    {
      try
      {
        it->second->stop();
      }
      catch (...)
      {
        detail::suppressException();
      }
      sessionServers.erase(it);
    }
  }

  auto removeServer(const std::optional<std::string> &sessionId) -> void
  {
    if (sessionId.has_value())
    {
      removeServer(*sessionId);
    }
  }

  auto stopAllServers() -> void
  {
    // Stop all per-session servers
    for (auto &pair : sessionServers)
    {
      try
      {
        pair.second->stop();
      }
      catch (...)
      {
        detail::suppressException();
      }
    }
    sessionServers.clear();

    // Stop single server if exists
    if (singleServer)
    {
      try
      {
        singleServer->stop();
      }
      catch (...)
      {
        detail::suppressException();
      }
      singleServer.reset();
    }
  }

  auto handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> transport::http::StreamableRequestResult
  {
    transport::http::StreamableRequestResult result;

    const bool requireSessionId = options.transportOptions.http.requireSessionId;

    // Determine the session ID to use for routing
    std::optional<std::string> routingSessionId;
    if (requireSessionId)
    {
      routingSessionId = context.sessionId;
    }
    else
    {
      routingSessionId = std::nullopt;
    }

    try
    {
      // Check if this is an initialize request for a new session (only for requireSessionId=true)
      const bool isInitializeRequest = (request.method == "initialize");
      const bool isNewSession = [&]() -> bool
      {
        if (!requireSessionId || !routingSessionId.has_value())
        {
          return false;
        }
        const auto it = sessionServers.find(*routingSessionId);
        return it == sessionServers.end();
      }();

      std::shared_ptr<Server> server;

      // For requireSessionId=true: create/start on first initialize for new session
      // For requireSessionId=false: use single server, start on first request
      if (requireSessionId && isNewSession)
      {
        // Create new server for this session
        server = serverFactory();
        sessionServers[*routingSessionId] = server;

        // Set up outbound message sender
        server->setOutboundMessageSender(
          [this, sessionId = *routingSessionId](const jsonrpc::RequestContext &msgContext, const jsonrpc::Message &message) -> void
          {
            // Use the session ID from the message context if available, otherwise use the stored session ID
            std::optional<std::string> targetSessionId = msgContext.sessionId.has_value() ? msgContext.sessionId : sessionId;
            if (!streamableServer.enqueueServerMessage(message, targetSessionId))
            {
              // Log error but don't throw - server-initiated messages are best-effort
            }
          });

        // Start the server before handling any messages
        server->start();
      }
      else if (!requireSessionId)
      {
        // Single server mode - create/start on first request
        if (!singleServer)
        {
          singleServer = serverFactory();

          // Set up outbound message sender
          singleServer->setOutboundMessageSender(
            [this](const jsonrpc::RequestContext &msgContext, const jsonrpc::Message &message) -> void
            {
              // In single-server mode, session ID is always nullopt
              if (!streamableServer.enqueueServerMessage(message, std::nullopt))
              {
                // Log error but don't throw - server-initiated messages are best-effort
              }
            });
        }

        if (!singleServerStarted_)
        {
          singleServer->start();
          singleServerStarted_ = true;
        }

        server = singleServer;
      }
      else
      {
        // Existing session - get the server
        server = getOrCreateServer(routingSessionId);
      }

      // Handle the request
      result.response = server->handleRequest(context, request).get();
    }
    catch (const std::exception &error)
    {
      // Return internal error response with original request ID
      result.response = jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(request.id, "Internal error"), request.id);
    }

    return result;
  }

  auto handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> bool
  {
    const bool requireSessionId = options.transportOptions.http.requireSessionId;

    std::optional<std::string> routingSessionId;
    if (requireSessionId)
    {
      routingSessionId = context.sessionId;
    }
    else
    {
      routingSessionId = std::nullopt;
    }

    try
    {
      std::shared_ptr<Server> server = getOrCreateServer(routingSessionId);
      server->handleNotification(context, notification);
      return true;
    }
    catch (const std::exception &)
    {
      // For notification dispatch exceptions, return false
      return false;
    }
  }

  auto handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool
  {
    const bool requireSessionId = options.transportOptions.http.requireSessionId;

    std::optional<std::string> routingSessionId;
    if (requireSessionId)
    {
      routingSessionId = context.sessionId;
    }
    else
    {
      routingSessionId = std::nullopt;
    }

    try
    {
      std::shared_ptr<Server> server = getOrCreateServer(routingSessionId);
      return server->handleResponse(context, response);
    }
    catch (const std::exception &)
    {
      // For response dispatch exceptions, return false
      return false;
    }
  }

  bool singleServerStarted_ = false;
};

StreamableHttpServerRunner::StreamableHttpServerRunner(ServerFactory serverFactory)
  : StreamableHttpServerRunner(std::move(serverFactory), StreamableHttpServerRunnerOptions {})
{
}

StreamableHttpServerRunner::StreamableHttpServerRunner(ServerFactory serverFactory, StreamableHttpServerRunnerOptions options)
  : impl_(std::make_unique<Impl>(std::move(serverFactory), std::move(options)))
{
}

StreamableHttpServerRunner::~StreamableHttpServerRunner()
{
  if (impl_->running.load())
  {
    stop();
  }
  impl_->stopAllServers();
}

StreamableHttpServerRunner::StreamableHttpServerRunner(StreamableHttpServerRunner &&other) noexcept = default;

auto StreamableHttpServerRunner::operator=(StreamableHttpServerRunner &&other) noexcept -> StreamableHttpServerRunner & = default;

auto StreamableHttpServerRunner::start() -> void
{
  if (impl_->running.load())
  {
    return;
  }

  // Set up handlers on the streamable server
  impl_->streamableServer.setRequestHandler([this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> transport::http::StreamableRequestResult
                                            { return impl_->handleRequest(context, request); });

  impl_->streamableServer.setNotificationHandler([this](const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> bool
                                                 { return impl_->handleNotification(context, notification); });

  impl_->streamableServer.setResponseHandler([this](const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool
                                             { return impl_->handleResponse(context, response); });

  // Set up the runtime to delegate to the streamable server
  impl_->runtime.setRequestHandler(
    [this](const transport::http::ServerRequest &request) -> transport::http::ServerResponse
    {
      const auto response = impl_->streamableServer.handleRequest(request);

      // Handle session cleanup on HTTP DELETE
      if (request.method == transport::http::ServerRequestMethod::kDelete)
      {
        const auto sessionIdHeader = transport::http::getHeader(request.headers, transport::http::kHeaderMcpSessionId);
        if (sessionIdHeader.has_value())
        {
          const std::string_view sessionId = transport::http::detail::trimAsciiWhitespace(*sessionIdHeader);
          if (transport::http::isValidSessionId(sessionId))
          {
            impl_->removeServer(std::string(sessionId));
          }
        }
      }

      // Handle session cleanup on HTTP 404
      if (response.statusCode == transport::http::detail::kHttpStatusNotFound)
      {
        const auto sessionIdHeader = transport::http::getHeader(request.headers, transport::http::kHeaderMcpSessionId);
        if (sessionIdHeader.has_value())
        {
          const std::string_view sessionId = transport::http::detail::trimAsciiWhitespace(*sessionIdHeader);
          if (transport::http::isValidSessionId(sessionId))
          {
            impl_->removeServer(std::string(sessionId));
          }
        }
      }

      return response;
    });

  // Start the runtime
  impl_->runtime.start();
  impl_->running.store(true);
}

auto StreamableHttpServerRunner::stop() -> void
{
  if (!impl_->running.load())
  {
    return;
  }

  impl_->running.store(false);

  // Stop the runtime
  impl_->runtime.stop();

  // Stop all server instances
  impl_->stopAllServers();
}

auto StreamableHttpServerRunner::isRunning() const noexcept -> bool
{
  return impl_->running.load();
}

auto StreamableHttpServerRunner::localPort() const noexcept -> std::uint16_t
{
  return impl_->runtime.localPort();
}

auto StreamableHttpServerRunner::options() const -> const StreamableHttpServerRunnerOptions &
{
  return impl_->options;
}

}  // namespace mcp
