#pragma once

/**
 * @brief HTTP transport umbrella header.
 *
 * This header includes all HTTP transport-related types.
 * Each type is defined in its own dedicated header file.
 */

// Basic HTTP types
#include <mcp/transport/http/header.hpp>

// TLS configuration
#include <mcp/transport/http/client_tls_configuration.hpp>
#include <mcp/transport/http/server_tls_configuration.hpp>

// Header utilities and constants
#include <mcp/transport/http/header_utils.hpp>

// Header state classes
#include <mcp/transport/http/protocol_version_header_state.hpp>
#include <mcp/transport/http/session_header_state.hpp>
#include <mcp/transport/http/shared_header_state.hpp>

// Session and request types
#include <mcp/transport/http/request_kind.hpp>
#include <mcp/transport/http/request_validation_options.hpp>
#include <mcp/transport/http/request_validation_result.hpp>
#include <mcp/transport/http/request_validator.hpp>
#include <mcp/transport/http/session_lookup_state.hpp>
#include <mcp/transport/http/session_resolution.hpp>

// Configuration types
#include <mcp/transport/http/http_client_options.hpp>
#include <mcp/transport/http/http_endpoint_config.hpp>
#include <mcp/transport/http/http_server_options.hpp>

// Request/Response types
#include <mcp/transport/http/server_request.hpp>
#include <mcp/transport/http/server_response.hpp>
#include <mcp/transport/http/sse_stream_response.hpp>
#include <mcp/transport/http/streamable_request_result.hpp>

// Server types
#include <mcp/transport/http/streamable_http_server.hpp>
#include <mcp/transport/http/streamable_http_server_options.hpp>

// Client types
#include <mcp/transport/http/streamable_http_client.hpp>
#include <mcp/transport/http/streamable_http_client_options.hpp>
#include <mcp/transport/http/streamable_http_listen_result.hpp>
#include <mcp/transport/http/streamable_http_send_result.hpp>

// Runtime types
#include <mcp/transport/http/http_client_runtime.hpp>
#include <mcp/transport/http/http_server_runtime.hpp>