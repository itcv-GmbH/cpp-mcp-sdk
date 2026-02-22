#pragma once

#include <functional>
#include <future>

#include <mcp/jsonrpc/message.hpp>
#include <mcp/jsonrpc/notification.hpp>
#include <mcp/jsonrpc/request.hpp>
#include <mcp/jsonrpc/request_context.hpp>
#include <mcp/jsonrpc/response.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Handler for JSON-RPC requests.
 *
 * @section Thread Safety
 *
 * @par Thread-Safety Classification: User-implemented
 *
 * RequestHandler is a type alias for std::function. Thread safety depends on the
 * implementation provided by the user.
 *
 * @par Callback Threading Rules:
 * - Serial invocation per request, router/I/O thread
 * - Must be fast and non-blocking
 * - Exceptions are caught and converted to JSON-RPC error responses with code -32603
 *
 * @section Exceptions
 *
 * Exceptions thrown by the handler are caught by dispatchRequest() and converted
 * to JSON-RPC error responses with code -32603 (Internal Error).
 */
using RequestHandler = std::function<std::future<Response>(const RequestContext &, const Request &)>;

/**
 * @brief Handler for JSON-RPC notifications.
 *
 * @section Thread Safety
 *
 * @par Thread-Safety Classification: User-implemented
 *
 * NotificationHandler is a type alias for std::function. Thread safety depends on the
 * implementation provided by the user.
 *
 * @par Callback Threading Rules:
 * - Serial invocation per notification, router/I/O thread
 * - Must be fast and non-blocking
 * - Exceptions are caught and reported via the error reporter
 *
 * @section Exceptions
 *
 * Exceptions thrown by the handler are caught by dispatchNotification() and reported
 * via the error reporter. The router continues operating after reporting the error.
 */
using NotificationHandler = std::function<void(const RequestContext &, const Notification &)>;

/**
 * @brief Callback for sending outbound JSON-RPC messages.
 *
 * @section Thread Safety
 *
 * @par Thread-Safety Classification: User-implemented
 *
 * OutboundMessageSender is a type alias for std::function. Thread safety depends on the
 * implementation provided by the user.
 *
 * @par Callback Threading Rules:
 * - Serial invocation per message
 * - Threading determined by caller
 * - Must be set before dispatching messages
 *
 * @section Exceptions
 *
 * May throw std::runtime_error on transport or serialization failure.
 */
using OutboundMessageSender = std::function<void(const RequestContext &, Message)>;

}  // namespace mcp::jsonrpc
