#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace mcp::detail
{

/// A unified abstraction for transport inbound loops (reader threads).
/// Provides consistent lifecycle management (start, stop, join) and error containment.
///
/// This class ensures:
/// - Exception containment: Loop body exceptions must not escape the thread
/// - Clean shutdown: stop() signals termination, join() waits for thread
/// - Thread-safe state: isRunning() is safe to call from any thread
/// - Idempotent: Multiple start/stop calls are safe
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
