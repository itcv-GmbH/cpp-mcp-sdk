#pragma once

#include <memory>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/session.hpp>

// Session is now defined in <mcp/session.hpp> as an alias for lifecycle::Session

namespace mcp::transport
{

/**
 * @brief Thread Safety and Lifecycle Contract
 *
 * @par Thread-Safety Classification: Thread-compatible
 *
 * The Transport base class defines the interface. Concrete implementations determine
 * their own thread-safety classification.
 *
 * @par Interface Methods:
 * - attach() - Called during setup phase, before start()
 * - start() - Idempotent; called to start transport operations
 * - stop() - Idempotent and noexcept; called during teardown
 * - isRunning() - Query method, noexcept
 * - send() - Called during operation
 *
 * @par Concurrency Rules:
 * 1. attach() must complete before start() is called.
 * 2. start() must complete before send() is called.
 * 3. isRunning() may be called at any time.
 * 4. stop() may be called multiple times safely (idempotent).
 *
 * @par Exception Guarantees:
 * - start() may throw on failure (e.g., cannot spawn subprocess, cannot connect)
 * - stop() must be noexcept; implementations must suppress all exceptions internally
 * - ~Transport() must be noexcept; destructors must not throw
 * - isRunning() is noexcept
 * - send() may throw on I/O errors
 *
 * @par Implementation Note:
 * Concrete implementations may provide stronger thread-safety guarantees.
 * Refer to the specific implementation's documentation for details.
 *
 * @par Thread Entrypoint Requirements:
 * All background threads created by transport implementations must:
 * 1. Have noexcept entrypoints
 * 2. Catch all exceptions and report them via the error reporter
 * 3. Never allow exceptions to escape to std::terminate
 */

class MCP_SDK_EXPORT Transport
{
public:
  Transport() = default;
  virtual ~Transport() = default;
  Transport(const Transport &) = delete;
  Transport(Transport &&) = delete;
  auto operator=(const Transport &) -> Transport & = delete;
  auto operator=(Transport &&) -> Transport & = delete;

  virtual auto attach(std::weak_ptr<Session> session) -> void = 0;
  virtual auto start() -> void = 0;
  virtual auto stop() -> void = 0;
  virtual auto isRunning() const noexcept -> bool = 0;
  virtual auto send(jsonrpc::Message message) -> void = 0;
};

}  // namespace mcp::transport
