#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <mcp/error_reporter.hpp>

namespace mcp::detail
{

/**
 * Wraps a callable in a noexcept boundary with exception reporting.
 *
 * This helper ensures that no exceptions escape the thread entrypoint,
 * preventing std::terminate from being called. Any caught exceptions
 * are reported via the unified error reporting mechanism.
 *
 * Usage: auto wrapped = threadBoundary(callable, reporter, "Component");
 */
template<typename Callable>
[[nodiscard]] inline auto threadBoundary(Callable callable, ErrorReporter errorReporter, std::string_view component) noexcept -> std::function<void()>
{
  return [callable = std::move(callable), errorReporter = std::move(errorReporter), component = std::string(component)]() noexcept -> void
  {
    try
    {
      callable();
    }
    catch (...)
    {
      reportCurrentException(errorReporter, component);
    }
  };
}

/**
 * Wraps a callable in a noexcept boundary for boost::asio::thread_pool work items.
 *
 * Similar to threadBoundary, but for thread pool work items.
 */
template<typename Callable>
[[nodiscard]] inline auto threadPoolWork(Callable callable, ErrorReporter errorReporter, std::string_view component) noexcept -> std::function<void()>
{
  return [callable = std::move(callable), errorReporter = std::move(errorReporter), component = std::string(component)]() mutable noexcept -> void
  {
    try
    {
      callable();
    }
    catch (...)
    {
      reportCurrentException(errorReporter, component);
    }
  };
}

}  // namespace mcp::detail
