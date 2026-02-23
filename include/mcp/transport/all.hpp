#pragma once

/**
 * @brief Transport module umbrella header.
 *
 * This header includes all transport types. Each type has its own dedicated
 * header file following the one-type-per-header rule. This umbrella header is
 * provided for convenience.
 *
 * @section Included Headers
 *
 * Base transport:
 * - @ref transport.hpp - Transport base class
 *
 * STDIO transport:
 * - @ref stdio_attach_options.hpp - StdioAttachOptions configuration
 * - @ref stdio_client_options.hpp - StdioClientOptions configuration
 * - @ref stdio_client_stderr_mode.hpp - StdioClientStderrMode enum
 * - @ref stdio_server_options.hpp - StdioServerOptions configuration
 * - @ref stdio_subprocess.hpp - StdioSubprocess class
 * - @ref stdio_subprocess_shutdown_options.hpp - StdioSubprocessShutdownOptions configuration
 * - @ref stdio_subprocess_spawn_options.hpp - StdioSubprocessSpawnOptions configuration
 * - @ref stdio_transport.hpp - StdioTransport class
 *
 * HTTP transport:
 * - @ref http/header.hpp - Header struct
 * - @ref http/client_tls_configuration.hpp - ClientTlsConfiguration struct
 * - @ref http/server_tls_configuration.hpp - ServerTlsConfiguration struct
 * - @ref http/header_utils.hpp - Header utility functions
 * - @ref http/protocol_version_header_state.hpp - ProtocolVersionHeaderState class
 * - @ref http/session_header_state.hpp - SessionHeaderState class
 * - @ref http/shared_header_state.hpp - SharedHeaderState class
 * - @ref http/request_kind.hpp - RequestKind enum
 * - @ref http/request_validation_options.hpp - RequestValidationOptions struct
 * - @ref http/request_validation_result.hpp - RequestValidationResult struct
 * - @ref http/request_validator.hpp - RequestValidator class
 * - @ref http/session_lookup_state.hpp - SessionLookupState enum
 * - @ref http/session_resolution.hpp - SessionResolution struct
 * - @ref http/http_endpoint_config.hpp - HttpEndpointConfig struct
 * - @ref http/http_client_options.hpp - HttpClientOptions struct
 * - @ref http/http_server_options.hpp - HttpServerOptions struct
 * - @ref http/server_request.hpp - ServerRequest struct
 * - @ref http/server_response.hpp - ServerResponse struct
 * - @ref http/sse_stream_response.hpp - SseStreamResponse struct
 * - @ref http/streamable_request_result.hpp - StreamableRequestResult struct
 * - @ref http/streamable_http_server.hpp - StreamableHttpServer class
 * - @ref http/streamable_http_server_options.hpp - StreamableHttpServerOptions struct
 * - @ref http/streamable_http_client.hpp - StreamableHttpClient class
 * - @ref http/streamable_http_client_options.hpp - StreamableHttpClientOptions struct
 * - @ref http/streamable_http_listen_result.hpp - StreamableHttpListenResult struct
 * - @ref http/streamable_http_send_result.hpp - StreamableHttpSendResult struct
 * - @ref http/http_client_runtime.hpp - HttpClientRuntime class
 * - @ref http/http_server_runtime.hpp - HttpServerRuntime class
 *
 * @section Thread Safety
 *
 * See individual type documentation for thread-safety classifications.
 *
 * @section Exceptions
 *
 * See individual type documentation for exception guarantees.
 */

// Base transport
#include <mcp/transport/transport.hpp>

// STDIO transport
#include <mcp/transport/stdio_attach_options.hpp>
#include <mcp/transport/stdio_client_options.hpp>
#include <mcp/transport/stdio_client_stderr_mode.hpp>
#include <mcp/transport/stdio_server_options.hpp>
#include <mcp/transport/stdio_subprocess.hpp>
#include <mcp/transport/stdio_subprocess_shutdown_options.hpp>
#include <mcp/transport/stdio_subprocess_spawn_options.hpp>
#include <mcp/transport/stdio_transport.hpp>

// HTTP transport
#include <mcp/transport/http/client_tls_configuration.hpp>
#include <mcp/transport/http/header.hpp>
#include <mcp/transport/http/header_utils.hpp>
#include <mcp/transport/http/http_client_options.hpp>
#include <mcp/transport/http/http_client_runtime.hpp>
#include <mcp/transport/http/http_endpoint_config.hpp>
#include <mcp/transport/http/http_server_options.hpp>
#include <mcp/transport/http/http_server_runtime.hpp>
#include <mcp/transport/http/protocol_version_header_state.hpp>
#include <mcp/transport/http/request_kind.hpp>
#include <mcp/transport/http/request_validation_options.hpp>
#include <mcp/transport/http/request_validation_result.hpp>
#include <mcp/transport/http/request_validator.hpp>
#include <mcp/transport/http/server_request.hpp>
#include <mcp/transport/http/server_response.hpp>
#include <mcp/transport/http/server_tls_configuration.hpp>
#include <mcp/transport/http/session_header_state.hpp>
#include <mcp/transport/http/session_lookup_state.hpp>
#include <mcp/transport/http/session_resolution.hpp>
#include <mcp/transport/http/shared_header_state.hpp>
#include <mcp/transport/http/sse_stream_response.hpp>
#include <mcp/transport/http/streamable_http_client.hpp>
#include <mcp/transport/http/streamable_http_client_options.hpp>
#include <mcp/transport/http/streamable_http_listen_result.hpp>
#include <mcp/transport/http/streamable_http_send_result.hpp>
#include <mcp/transport/http/streamable_http_server.hpp>
#include <mcp/transport/http/streamable_http_server_options.hpp>
#include <mcp/transport/http/streamable_request_result.hpp>
