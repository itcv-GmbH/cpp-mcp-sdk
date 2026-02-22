#pragma once

/**
 * @brief STDIO transport module umbrella header.
 *
 * This header includes all STDIO transport types. Each type has its own dedicated
 * header file following the one-type-per-header rule. This umbrella header is
 * provided for convenience and backward compatibility.
 *
 * @section Included Headers
 *
 * - @ref stdio_client_options.hpp - StdioClientOptions configuration
 * - @ref stdio_client_stderr_mode.hpp - StdioClientStderrMode enum
 * - @ref stdio_server_options.hpp - StdioServerOptions configuration
 * - @ref stdio_attach_options.hpp - StdioAttachOptions configuration
 * - @ref stdio_subprocess.hpp - StdioSubprocess class
 * - @ref stdio_subprocess_shutdown_options.hpp - StdioSubprocessShutdownOptions configuration
 * - @ref stdio_subprocess_spawn_options.hpp - StdioSubprocessSpawnOptions configuration
 * - @ref stdio_transport.hpp - StdioTransport class
 *
 * @section Thread Safety
 *
 * See individual type documentation for thread-safety classifications.
 *
 * @section Exceptions
 *
 * See individual type documentation for exception guarantees.
 */

#include <mcp/transport/stdio_attach_options.hpp>
#include <mcp/transport/stdio_client_options.hpp>
#include <mcp/transport/stdio_client_stderr_mode.hpp>
#include <mcp/transport/stdio_server_options.hpp>
#include <mcp/transport/stdio_subprocess.hpp>
#include <mcp/transport/stdio_subprocess_shutdown_options.hpp>
#include <mcp/transport/stdio_subprocess_spawn_options.hpp>
#include <mcp/transport/stdio_transport.hpp>
