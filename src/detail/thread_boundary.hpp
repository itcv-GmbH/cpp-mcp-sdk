#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
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
[[nodiscard]] inline auto threadBoundary(Callable callable, ErrorReporter errorReporter, std::string_view component)
{
  auto callablePtr = std::make_shared<std::decay_t<Callable>>(std::move(callable));

  // Note: Not marked noexcept because closure construction can throw (bad_alloc).
  // NOLINTNEXTLINE(bugprone-exception-escape) - Exceptions from callable are contained within the boundary try/catch below.
  return [callable = std::move(callablePtr), errorReporter = std::move(errorReporter), component = std::string(component)]() noexcept -> void
  {
    try
    {
      (*callable)();
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
[[nodiscard]] inline auto threadPoolWork(Callable callable, ErrorReporter errorReporter, std::string_view component)
{
  auto callablePtr = std::make_shared<std::decay_t<Callable>>(std::move(callable));

  // Note: Not marked noexcept because closure construction can throw (bad_alloc).
  return [callable = std::move(callablePtr), errorReporter = std::move(errorReporter), component = std::string(component)]() noexcept -> void
  {
    try
    {
      (*callable)();
    }
    catch (...)
    {
      reportCurrentException(errorReporter, component);
    }
  };
}

}  // namespace mcp::detail
