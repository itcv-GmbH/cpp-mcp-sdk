#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <mcp/server/combined_runner.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/server/streamable_http_runner.hpp>

namespace
{

constexpr std::string_view kName = "CombinedServerRunner";  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

namespace detail
{

// No-op helper for suppressing exceptions in destructors
inline auto suppressException() noexcept -> void {}  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

}  // namespace detail

}  // namespace

namespace mcp
{

struct CombinedServerRunner::Impl
{
  explicit Impl(ServerFactory serverFactoryIn, CombinedServerRunnerOptions optionsIn)
    : serverFactory(std::move(serverFactoryIn))
    , options(std::move(optionsIn))
  {
    if (options.enableStdio)
    {
      stdioRunner = std::make_unique<StdioServerRunner>(serverFactory, options.stdioOptions);
    }
    if (options.enableHttp)
    {
      httpRunner = std::make_unique<StreamableHttpServerRunner>(serverFactory, options.httpOptions);
    }
  }

  ServerFactory serverFactory;
  CombinedServerRunnerOptions options;

  std::unique_ptr<StdioServerRunner> stdioRunner;
  std::unique_ptr<StreamableHttpServerRunner> httpRunner;

  std::atomic<bool> httpRunning {false};
};

CombinedServerRunner::CombinedServerRunner(ServerFactory serverFactory, CombinedServerRunnerOptions options)
  : impl_(std::make_unique<Impl>(std::move(serverFactory), std::move(options)))
{
}

CombinedServerRunner::~CombinedServerRunner()
{
  // Ensure HTTP is stopped if it was running
  if (impl_->httpRunning.load())
  {
    try
    {
      stop();
    }
    catch (...)
    {
      // Suppress exceptions in destructor - must not throw
      ::detail::suppressException();
    }
  }
}

CombinedServerRunner::CombinedServerRunner(CombinedServerRunner &&other) noexcept = default;

auto CombinedServerRunner::operator=(CombinedServerRunner &&other) noexcept -> CombinedServerRunner &
{
  if (this != &other)
  {
    // Ensure HTTP is stopped before moving
    if (impl_->httpRunning.load())
    {
      stop();
    }
    impl_ = std::move(other.impl_);
  }
  return *this;
}

auto CombinedServerRunner::start() -> void
{
  // Start HTTP in background if enabled
  if (impl_->options.enableHttp)
  {
    startHttp();
  }

  // Run STDIO in foreground if enabled
  if (impl_->options.enableStdio)
  {
    runStdio();
  }
}

auto CombinedServerRunner::stop() -> void
{
  // Stop HTTP if running
  if (impl_->options.enableHttp && impl_->httpRunning.load())
  {
    stopHttp();
  }
}

auto CombinedServerRunner::runStdio() -> void
{
  if (!impl_->options.enableStdio)
  {
    throw std::runtime_error("STDIO transport is not enabled");
  }

  if (impl_->stdioRunner == nullptr)
  {
    throw std::runtime_error("STDIO runner is not initialized");
  }

  // Run STDIO - this blocks until EOF
  impl_->stdioRunner->run();

  // After STDIO exits (EOF), stop HTTP if it's running
  // This matches the documented behavior: "on STDIO EOF it must stop HTTP before returning"
  if (impl_->options.enableHttp && impl_->httpRunning.load())
  {
    stopHttp();
  }
}

auto CombinedServerRunner::startHttp() -> void
{
  if (!impl_->options.enableHttp)
  {
    throw std::runtime_error("HTTP transport is not enabled");
  }

  if (impl_->httpRunner == nullptr)
  {
    throw std::runtime_error("HTTP runner is not initialized");
  }

  impl_->httpRunner->start();
  impl_->httpRunning.store(true);
}

auto CombinedServerRunner::stopHttp() -> void
{
  if (impl_->httpRunner == nullptr)
  {
    return;
  }

  if (impl_->httpRunning.load())
  {
    impl_->httpRunner->stop();
    impl_->httpRunning.store(false);
  }
}

auto CombinedServerRunner::isHttpRunning() const noexcept -> bool
{
  return impl_->httpRunning.load();
}

auto CombinedServerRunner::localPort() const noexcept -> std::uint16_t
{
  if (impl_->httpRunner == nullptr || !impl_->httpRunning.load())
  {
    return 0;
  }
  return impl_->httpRunner->localPort();
}

auto CombinedServerRunner::options() const -> const CombinedServerRunnerOptions &
{
  return impl_->options;
}

auto CombinedServerRunner::stdioRunner() -> StdioServerRunner *
{
  return impl_->stdioRunner.get();
}

auto CombinedServerRunner::httpRunner() -> StreamableHttpServerRunner *
{
  return impl_->httpRunner.get();
}

}  // namespace mcp
