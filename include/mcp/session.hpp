#pragma once

/**
 * @file session.hpp
 * @brief Compatibility header providing mcp::Session alias.
 *
 * This header provides backward-compatible aliases for types in mcp::lifecycle and mcp::lifecycle::session.
 * New code should include <mcp/lifecycle/session.hpp> and use the canonical namespaces directly.
 */

#include <mcp/lifecycle/session.hpp>
#include <mcp/sdk/error_reporter.hpp>

namespace mcp
{

/**
 * @brief Backward-compatible alias for mcp::lifecycle::Session.
 * @deprecated Use mcp::lifecycle::Session instead.
 */
using Session = lifecycle::Session;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::SessionOptions.
 * @deprecated Use mcp::lifecycle::session::SessionOptions instead.
 */
using SessionOptions = lifecycle::session::SessionOptions;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::RequestOptions.
 * @deprecated Use mcp::lifecycle::session::RequestOptions instead.
 */
using RequestOptions = lifecycle::session::RequestOptions;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::ResponseCallback.
 * @deprecated Use mcp::lifecycle::session::ResponseCallback instead.
 */
using ResponseCallback = lifecycle::session::ResponseCallback;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::SessionRole.
 * @deprecated Use mcp::lifecycle::session::SessionRole instead.
 */
using SessionRole = lifecycle::session::SessionRole;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::SessionState.
 * @deprecated Use mcp::lifecycle::session::SessionState instead.
 */
using SessionState = lifecycle::session::SessionState;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::ClientCapabilities.
 * @deprecated Use mcp::lifecycle::session::ClientCapabilities instead.
 */
using ClientCapabilities = lifecycle::session::ClientCapabilities;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::ServerCapabilities.
 * @deprecated Use mcp::lifecycle::session::ServerCapabilities instead.
 */
using ServerCapabilities = lifecycle::session::ServerCapabilities;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::NegotiatedParameters.
 * @deprecated Use mcp::lifecycle::session::NegotiatedParameters instead.
 */
using NegotiatedParameters = lifecycle::session::NegotiatedParameters;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::Implementation.
 * @deprecated Use mcp::lifecycle::session::Implementation instead.
 */
using Implementation = lifecycle::session::Implementation;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::CapabilityError.
 * @deprecated Use mcp::lifecycle::session::CapabilityError instead.
 */
using CapabilityError = lifecycle::session::CapabilityError;

/**
 * @brief Backward-compatible alias for mcp::lifecycle::session::LifecycleError.
 * @deprecated Use mcp::lifecycle::session::LifecycleError instead.
 */
using LifecycleError = lifecycle::session::LifecycleError;

/**
 * @brief Backward-compatible alias for mcp::sdk::ErrorReporter.
 * @deprecated Use mcp::sdk::ErrorReporter instead.
 */
using ErrorReporter = sdk::ErrorReporter;

}  // namespace mcp
