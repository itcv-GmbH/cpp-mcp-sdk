#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <mcp/sdk/error_reporter.hpp>

namespace mcp::detail
{
/**
 * @brief A unified abstraction for transport inbound loops (reader threads).
 *
 * Provides consistent lifecycle management (start, stop, join) and error containment.
 *
 * @section Exceptions
 *
 * @subsection Construction
 * - InboundLoop(LoopBody) may throw std::bad_alloc on memory allocation failure
 *
 * @subsection Destruction
 * - ~InboundLoop() default destructor
 *
 * @subsection Lifecycle Operations
 * - start() may throw std::runtime_error if thread creation fails
 * - stop() sets atomic flag
 * - join() waits for thread completion
 * - isRunning() noexcept - atomic state check
 *
 * @subsection Loop Body Behavior
 * The LoopBody function is invoked in a background thread. The current implementation
 * may catch exceptions from the loop body, but this is not a guaranteed contract.
 * Write loop bodies that handle their own exceptions.
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
 * @subsection Thread Safety
 * - stop(), join(), isRunning() are safe to call from any thread
 * - Multiple start/stop calls are handled safely
 */
class InboundLoop
{
public:
  using LoopBody = std::function<void()>;

  /// Constructs an inbound loop with a body function and optional error reporter.
  /// @param body The loop body function to execute
  /// @param errorReporter Optional error reporter for background execution failures
  explicit InboundLoop(LoopBody body, ErrorReporter errorReporter = {});
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
