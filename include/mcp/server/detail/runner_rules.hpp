#pragma once

#include <string_view>

namespace mcp::server
{

/// @brief Runner behavior rules for different transport configurations.
///
/// ## STDIO Runner
/// - Uses exactly one Server instance for its entire lifetime.
/// - Calls Server::start() once during runner initialization.
/// - Calls Server::stop() when the runner is stopped or destroyed.
///
/// ## Streamable HTTP Runner
/// ### requireSessionId=true (multi-client mode)
/// - Creates a new Server instance per unique MCP-Session-Id.
/// - Session creation is triggered on the first accepted "initialize" request for a newly issued
///   MCP-Session-Id.
/// - Uses RequestContext.sessionId as the key to track per-session servers.
/// - Treats missing sessionId in requests as an internal error.
/// - Each per-session Server instance must call Server::start() before handling any messages.
/// - Each per-session Server instance must call Server::stop() before being dropped.
///
/// ### requireSessionId=false (single-client mode)
/// - Uses exactly one Server instance for all requests.
/// - Treats RequestContext.sessionId as std::nullopt.
/// - Server instance is created and started on first accepted "initialize" request (not during runner initialization).
/// - Does not provide per-session isolation; all clients share the same Server instance.
/// - Calls Server::stop() when the runner is stopped or destroyed.
///
/// ## Combined Runner
/// - Creates one Server instance for STDIO transport.
/// - Creates one Server instance per HTTP session when requireSessionId=true.
/// - Uses the same lifecycle rules as individual runners for each transport.
///
/// @note Session isolation is critical because mcp::lifecycle::Session lifecycle state (e.g., protocol version
/// negotiation, capability initialization) is per-instance. Routing multiple sessions through a
/// single Server instance would cause initialization/lifecycle state to be shared incorrectly.
namespace runner
{

/// @brief Session isolation rules for Streamable HTTP runners.
namespace session_rules
{

/// @brief Defines when a new HTTP session server is created.
/// @details A new Server instance is created on the first accepted "initialize" request for a
/// newly issued MCP-Session-Id. This ensures each session has its own initialized state.
inline constexpr std::string_view kSessionCreationTrigger = "on first accepted 'initialize' request for a newly issued MCP-Session-Id";

/// @brief Cleanup trigger: HTTP DELETE
/// @details When an HTTP DELETE request is received for a specific sessionId, the runner must
/// drop the corresponding Server instance after calling Server::stop().
inline constexpr std::string_view kCleanupTriggerHttpDelete = "drop per-session servers on HTTP DELETE for the sessionId";

/// @brief Cleanup trigger: HTTP 404
/// @details When the transport returns HTTP 404 for a session (indicating the session is expired
/// or terminated), the runner must drop the corresponding Server instance after calling
/// Server::stop().
inline constexpr std::string_view kCleanupTriggerHttp404 = "drop per-session servers when the transport returns HTTP 404 for that session (expired/terminated)";

/// @brief Session-keying behavior for requireSessionId=true
/// @details When StreamableHttpServerOptions.http.requireSessionId is true:
/// - Use RequestContext.sessionId as the key to track and route requests to the correct
///   per-session Server instance.
/// - Treat missing sessionId in requests as an internal error.
inline constexpr std::string_view kSessionKeyingRequireSessionIdTrue = "use RequestContext.sessionId as the key; treat missing sessionId as an internal error";

/// @brief Session-keying behavior for requireSessionId=false
/// @details When StreamableHttpServerOptions.http.requireSessionId is false:
/// - Use a single fixed key for all requests (all requests route to the same Server instance).
/// - Treat RequestContext.sessionId as std::nullopt (the header is ignored).
inline constexpr std::string_view kSessionKeyingRequireSessionIdFalse = "use a single fixed key for all requests; treat RequestContext.sessionId as std::nullopt";

}  // namespace session_rules

/// @brief Runner lifecycle rules for Server instances.
namespace lifecycle_rules
{

/// @brief Start requirement
/// @details The runner must call mcp::server::Server::start() for every Server instance it creates
/// before handling any messages. This ensures the session is properly initialized and ready to
/// process requests.
inline constexpr std::string_view kStartRequirement = "runner must call mcp::server::Server::start() for every server instance it creates before handling any messages";

/// @brief Stop triggers
/// @details The runner must call mcp::server::Server::stop() before dropping a Server instance due to:
/// - HTTP DELETE for the session
/// - HTTP 404 cleanup (session expired/terminated)
/// - Runner stop
/// - Runner destruction
inline constexpr std::string_view kStopTriggers =
  "runner must call mcp::server::Server::stop() before dropping a server instance due to HTTP DELETE, "
  "HTTP 404 cleanup, runner stop, or runner destruction";

}  // namespace lifecycle_rules

}  // namespace runner

}  // namespace mcp::server
