#include <atomic>
#include <memory>
#include <thread>
#include <utility>

#include "mcp/detail/inbound_loop.hpp"

#include "mcp/detail/thread_boundary.hpp"
#include "mcp/sdk/error_reporter.hpp"

namespace mcp::detail
{

class InboundLoop::Impl
{
public:
  explicit Impl(LoopBody body, ErrorReporter errorReporter)
    : body_(std::move(body))
    , errorReporter_(std::move(errorReporter))
    , running_(false)
  {
  }

  void start()
  {
    if (running_.load())
    {
      return;
    }

    running_.store(true);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    thread_ = std::thread(threadBoundary([this]() -> void { runLoop(); }, errorReporter_, "InboundLoop"));
  }

  void stop() { running_.store(false); }

  void join()
  {
    if (thread_.joinable())
    {
      thread_.join();
    }
  }

  [[nodiscard]] auto isRunning() const noexcept -> bool { return running_.load(); }

private:
  void runLoop()
  {
    // NOLINTNEXTLINE(bugprone-exception-escape) - Exception containment is intentional
    try
    {
      if (body_)
      {
        body_();
      }
    }
    catch (...)
    {
      // Exception containment: Prevent exceptions from escaping the thread boundary.
      // Report the exception through the error reporter if configured.
      reportCurrentException(errorReporter_, "InboundLoop");
    }

    running_.store(false);
  }

  LoopBody body_;
  ErrorReporter errorReporter_;
  std::atomic<bool> running_;
  std::thread thread_;
};

InboundLoop::InboundLoop(LoopBody body, ErrorReporter errorReporter)
  : impl_(std::make_unique<Impl>(std::move(body), std::move(errorReporter)))
{
}

InboundLoop::~InboundLoop()
{
  stop();
  join();
}

void InboundLoop::start()
{
  impl_->start();
}

void InboundLoop::stop()
{
  impl_->stop();
}

void InboundLoop::join()
{
  impl_->join();
}

auto InboundLoop::isRunning() const noexcept -> bool
{
  return impl_->isRunning();
}

}  // namespace mcp::detail
