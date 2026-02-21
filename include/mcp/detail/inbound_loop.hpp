#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace mcp::detail
{
/**
 * @brief A unified abstraction for transport inbound loops (reader threads).
 *
 * Provides consistent lifecycle management (start, stop, join) and error containment.
 *
 * @section Exceptions
 *
 * @subsection Thread Safety
 * - Exception containment: Loop body exceptions are caught and suppressed; they never escape
 *   the thread. This prevents std::terminate from uncaught exceptions in background threads.
 * - Clean shutdown: stop() signals termination, join() waits for thread
 * - Thread-safe state: isRunning() is safe to call from any thread
 * - Idempotent: Multiple start/stop calls are safe
 *
 * @subsection Construction
 * - InboundLoop(LoopBody) may throw std::bad_alloc on memory allocation failure
 *
 * @subsection Destruction
 * - ~InboundLoop() is implicitly noexcept. It automatically calls stop() and join().
 *
 * @subsection Lifecycle Operations
 * - start() may throw std::runtime_error if thread creation fails (rare, system limit)
 * - stop() noexcept - safe to call from any thread, sets atomic flag
 * - join() noexcept - waits for thread completion, safe to call multiple times
 * - isRunning() noexcept - atomic state check
 *
 * @subsection Loop Body Requirements
 * The LoopBody function passed to the constructor MUST NOT throw exceptions that escape.
 * While the InboundLoop catches all exceptions from the loop body, throwing exceptions
 * from the loop body will:
 * - Trigger exception handling overhead
 * - Potentially terminate the loop unexpectedly
 *
 * Recommended pattern for loop body:
 * @code
 * InboundLoop loop([]() {
 *     try {
 *         // Read and process messages
 *     } catch (const std::exception& e) {
 *         // Log or report error, but keep loop running if possible
 *     }
 * });
 * @endcode
 *
 * @subsection Exception Guarantees
 * - Basic guarantee for all operations
 * - Noexcept guarantee for stop(), join(), isRunning(), and destructor
 * - Loop body exceptions are caught and suppressed (do not propagate)
 */
class InboundLoop
{
public:
  using LoopBody = std::function<void()>;

  explicit InboundLoop(LoopBody body);
  ~InboundLoop();

  InboundLoop(const InboundLoop &) = delete;
  auto operator=(const InboundLoop &) -> InboundLoop & = delete;
  InboundLoop(InboundLoop &&) = delete;
  auto operator=(InboundLoop &&) -> InboundLoop & = delete;

  /// Starts the inbound loop by spawning a thread that runs the loop body.
  /// If already running, this is a no-op.
  void start();

  /// Stops the inbound loop by clearing the run flag.
  /// Does not wait for the thread to finish - call join() for that.
  void stop();

  /// Joins the thread, waiting for it to complete.
  /// If the thread is not running, this is a no-op.
  void join();

  /// Returns whether the loop is currently running.
  /// Thread-safe and can be called from any thread.
  [[nodiscard]] auto isRunning() const noexcept -> bool;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::detail
