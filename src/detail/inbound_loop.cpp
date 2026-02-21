#include <atomic>
#include <memory>
#include <thread>

#include <mcp/detail/inbound_loop.hpp>

namespace mcp::detail
{

class InboundLoop::Impl
{
public:
  explicit Impl(LoopBody body)
    : body_(std::move(body))
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
    thread_ = std::thread([this]() -> void { runLoop(); });
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
      // In production, you might want to log or store the exception for later inspection.
      // For now, we silently swallow exceptions to maintain stable transport operation.
    }

    running_.store(false);
  }

  LoopBody body_;
  std::atomic<bool> running_;
  std::thread thread_;
};

InboundLoop::InboundLoop(LoopBody body)
  : impl_(std::make_unique<Impl>(std::move(body)))
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
