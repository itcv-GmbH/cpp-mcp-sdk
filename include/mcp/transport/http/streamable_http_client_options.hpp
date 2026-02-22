#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <mcp/error_reporter.hpp>
#include <mcp/security/limits.hpp>
#include <mcp/transport/http/client_tls_configuration.hpp>
#include <mcp/transport/http/header_utils.hpp>
#include <mcp/transport/http/shared_header_state.hpp>

namespace mcp::transport::http
{

struct StreamableHttpClientOptions
{
  std::string endpointUrl;
  std::optional<std::string> bearerToken;
  ClientTlsConfiguration tls;
  security::RuntimeLimits limits;
  std::shared_ptr<SharedHeaderState> headerState = std::make_shared<SharedHeaderState>();
  std::optional<bool> enableLegacyHttpSseFallback;
  std::string legacyFallbackPostPath = "/rpc";
  std::string legacyFallbackSsePath = "/events";
  std::uint32_t defaultRetryMilliseconds = detail::kDefaultRetryMilliseconds;
  std::function<void(std::uint32_t)> waitBeforeReconnect;

  // Enable GET SSE listen behavior for server-initiated messages.
  // When enabled, the client will use HTTP GET requests to listen for server messages
  // via SSE (Server-Sent Events), as specified in MCP 2025-11-25 transport spec section
  // "Listening for Messages from the Server". If the server returns HTTP 405 (Method Not
  // Allowed) for GET requests, this is treated as a supported configuration and the client
  // will fall back to POST-based message retrieval.
  bool enableGetListen = true;

  /// Error reporter callback for background execution context failures.
  /// If not set, errors are silently suppressed.
  ErrorReporter errorReporter;
};

}  // namespace mcp::transport::http