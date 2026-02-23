#pragma once

#include <functional>
#include <memory>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/all.hpp>
#include <mcp/transport/transport.hpp>

namespace mcp::transport
{

/**
 * @brief Creates a Streamable HTTP client transport.
 *
 * @section Thread Safety
 *
 * The returned Transport implementation is thread-compatible. External synchronization
 * is required for concurrent access from multiple threads.
 *
 * @par Thread-Safety Classification: Thread-compatible
 *
 * @section Exceptions
 *
 * The returned Transport implementation provides the following exception guarantees:
 * - attach(): Does not throw
 * - start(): Idempotent; does not throw
 * - stop(): Idempotent and noexcept; never throws
 * - isRunning() noexcept: Safe to call from any thread
 * - send(): May throw std::runtime_error if transport is not running or on I/O error
 *
 * @par Lifecycle
 * 1. Create the transport using makeStreamableHttpClientTransport()
 * 2. Call start() to begin operation (idempotent - safe to call multiple times)
 * 3. Use send() to send messages
 * 4. Call stop() to shut down (idempotent - safe to call multiple times)
 * 5. Destroy the transport (destructor is noexcept and calls stop() internally)
 *
 * @par Background Threads
 * The transport creates a background thread for the GET listen loop when
 * notifications/initialized is sent. This thread:
 * - Has a noexcept entrypoint (all exceptions are caught)
 * - Reports errors via the ErrorReporter callback configured in options
 * - Is joined deterministically in stop()
 *
 * @param options Configuration options for the HTTP client
 * @param requestExecutor Function to execute HTTP requests
 * @param inboundMessageHandler Callback for received messages
 * @return std::shared_ptr<Transport> A Transport implementation
 */
auto makeStreamableHttpClientTransport(http::StreamableHttpClientOptions options,
                                       http::StreamableHttpClient::RequestExecutor requestExecutor,
                                       std::function<void(const jsonrpc::Message &)> inboundMessageHandler) -> std::shared_ptr<Transport>;

}  // namespace mcp::transport
