#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Progress update information for long-running operations.
 */
struct ProgressUpdate
{
  RequestId progressToken;
  double progress = 0.0;
  std::optional<double> total;
  std::optional<std::string> message;
  JsonValue additionalProperties = JsonValue::object();
};

/**
 * @brief Callback for progress updates.
 *
 * @section Thread Safety
 *
 * @par Thread-Safety Classification: User-implemented
 *
 * ProgressCallback is a type alias for std::function. Thread safety depends on the
 * implementation provided by the user.
 *
 * @par Callback Threading Rules:
 * - Serial invocation per progress token, router/I/O thread
 * - Must be fast and non-blocking
 * - Exceptions are caught and reported via the error reporter
 *
 * @section Exceptions
 *
 * Exceptions thrown by the callback are caught and reported via the error reporter.
 */
using ProgressCallback = std::function<void(const RequestContext &, const ProgressUpdate &)>;

}  // namespace mcp::jsonrpc
