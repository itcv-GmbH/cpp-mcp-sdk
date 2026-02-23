#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <mcp/server/combined_runner.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include "mcp/server/server_factory.hpp"
#include "mcp/server/combined_server_runner_options.hpp"

namespace
{

constexpr std::string_view kName = "CombinedServerRunner";  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

namespace detail
{

// No-op helper for suppressing exceptions in destructors
inline auto suppressException() noexcept -> void {}  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

}  // namespace detail

}  // namespace

namespace mcp::server
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
};

CombinedServerRunner::CombinedServerRunner(ServerFactory serverFactory, CombinedServerRunnerOptions options)
  : impl_(std::make_unique<Impl>(std::move(serverFactory), std::move(options)))
{
}

CombinedServerRunner::~CombinedServerRunner()
{
  // Ensure HTTP is stopped if it was running - must be noexcept-safe
  if (impl_ && impl_->httpRunner && impl_->httpRunner->isRunning())
  {
    try
    {
      impl_->httpRunner->stop();
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
    // Stop HTTP if running before moving - must be noexcept-safe
    if (impl_ && impl_->httpRunner && impl_->httpRunner->isRunning())
    {
      try
      {
        impl_->httpRunner->stop();
      }
      catch (...)
      {
        // Suppress exceptions - must not throw
        ::detail::suppressException();
      }
    }
    impl_ = std::move(other.impl_);
  }
  return *this;
}

auto CombinedServerRunner::start() -> void
{
  if (!impl_)
  {
    return;
  }

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
  if (!impl_)
  {
    return;
  }

  // Stop HTTP if running - derive from underlying runner state
  if (impl_->options.enableHttp && impl_->httpRunner && impl_->httpRunner->isRunning())
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
  // Use custom streams if provided in options
  if (impl_->options.stdioInput != nullptr && impl_->options.stdioOutput != nullptr && impl_->options.stdioError != nullptr)
  {
    impl_->stdioRunner->run(*impl_->options.stdioInput, *impl_->options.stdioOutput, *impl_->options.stdioError);
  }
  else
  {
    impl_->stdioRunner->run();
  }

  // After STDIO exits (EOF), stop HTTP if it's running
  // This matches the documented behavior: "on STDIO EOF it must stop HTTP before returning"
  if (impl_->options.enableHttp && impl_->httpRunner && impl_->httpRunner->isRunning())
  {
    stopHttp();
  }
}

auto CombinedServerRunner::startHttp() -> void
{
  if (!impl_)
  {
    return;
  }

  if (!impl_->options.enableHttp)
  {
    throw std::runtime_error("HTTP transport is not enabled");
  }

  if (impl_->httpRunner == nullptr)
  {
    throw std::runtime_error("HTTP runner is not initialized");
  }

  impl_->httpRunner->start();
}

auto CombinedServerRunner::stopHttp() -> void
{
  if (!impl_)
  {
    return;
  }

  if (impl_->httpRunner == nullptr)
  {
    return;
  }

  // Derive running state from underlying runner
  if (impl_->httpRunner->isRunning())
  {
    impl_->httpRunner->stop();
  }
}

auto CombinedServerRunner::isHttpRunning() const noexcept -> bool
{
  return impl_ && impl_->httpRunner && impl_->httpRunner->isRunning();
}

auto CombinedServerRunner::localPort() const noexcept -> std::uint16_t
{
  if (!impl_ || impl_->httpRunner == nullptr || !impl_->httpRunner->isRunning())
  {
    return 0;
  }
  return impl_->httpRunner->localPort();
}

auto CombinedServerRunner::options() const -> const CombinedServerRunnerOptions &
{
  static const CombinedServerRunnerOptions defaultOptions;
  if (!impl_)
  {
    return defaultOptions;
  }
  return impl_->options;
}

auto CombinedServerRunner::stdioRunner() -> StdioServerRunner *
{
  if (!impl_)
  {
    return nullptr;
  }
  return impl_->stdioRunner.get();
}

auto CombinedServerRunner::httpRunner() -> StreamableHttpServerRunner *
{
  if (!impl_)
  {
    return nullptr;
  }
  return impl_->httpRunner.get();
}

}  // namespace mcp::server
