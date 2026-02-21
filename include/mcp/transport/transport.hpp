#pragma once

#include <memory>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

class Session;

namespace transport
{

/**
 * @brief Thread Safety
 *
 * @par Thread-Safety Classification: Thread-compatible
 *
 * The Transport base class defines the interface. Concrete implementations determine
 * their own thread-safety classification.
 *
 * @par Interface Methods:
 * - attach() - Called during setup phase, before start()
 * - start() - Called once during lifecycle
 * - stop() - Called during teardown
 * - isRunning() - Query method
 * - send() - Called during operation
 *
 * @par Concurrency Rules:
 * 1. attach() must complete before start() is called.
 * 2. start() must complete before send() is called.
 * 3. isRunning() may be called at any time.
 * 4. stop() must be called after start() and before destruction.
 *
 * @par Implementation Note:
 * Concrete implementations may provide stronger thread-safety guarantees.
 * Refer to the specific implementation's documentation for details.
 */

class Transport
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

}  // namespace transport
}  // namespace mcp
