#pragma once

#include <exception>
#include <functional>
#include <string>
#include <string_view>

#include <mcp/export.hpp>

namespace mcp
{
namespace sdk
{

namespace detail
{

inline auto suppressReporterException() noexcept -> void {}

}  // namespace detail

/**
 * @brief Error event type containing component identifier and error message.
 *
 * This structure represents an error that occurred in a background execution
 * context within the SDK. It is passed to the error reporter callback when
 * exceptions are caught in background threads.
 *
 * @section Thread Safety
 *
 * ErrorEvent is an immutable value type. Instances can be safely copied and
 * passed between threads without synchronization.
 *
 * @section Exceptions
 *
 * All methods provide the noexcept guarantee:
 * - ErrorEvent() noexcept
 * - ErrorEvent(component, message) noexcept (move operations on strings may throw, but are nothrow for empty strings)
 * - component() noexcept
 * - message() noexcept
 */
class MCP_SDK_EXPORT ErrorEvent
{
public:
  /// Constructs an empty error event.
  ErrorEvent() noexcept = default;

  /// Constructs an error event with component identifier and message.
  ErrorEvent(std::string component, std::string message) noexcept
    : component_(std::move(component))
    , message_(std::move(message))
  {
  }

  /// Returns the component identifier where the error occurred.
  [[nodiscard]] auto component() const noexcept -> std::string_view { return component_; }

  /// Returns the error message describing what went wrong.
  [[nodiscard]] auto message() const noexcept -> std::string_view { return message_; }

private:
  std::string component_;
  std::string message_;
};

/**
 * @brief Error reporter callback type.
 *
 * This callback is invoked when the SDK catches an exception in a background
 * execution context. The callback is treated as potentially throwing and is
 * always invoked from a catch-all boundary to ensure process stability.
 *
 * Important safety guarantees:
 * - The callback is invoked only from catch(...) blocks
 * - Any exception thrown by the callback is caught and suppressed
 * - The SDK continues operating after reporting the error
 * - The callback may be invoked from any thread
 *
 * @section Thread Safety
 *
 * The error reporter callback may be invoked from multiple threads concurrently.
 * Implementations must provide their own synchronization if state is shared.
 *
 * @section Usage Example
 *
 * @code
 * auto errorReporter = [](const mcp::sdk::ErrorEvent& event) {
 *     try {
 *         std::cerr << "[" << event.component() << "] " << event.message() << std::endl;
 *     } catch (...) {
 *         // Suppress any exceptions to prevent process termination
 *     }
 * };
 *
 * mcp::lifecycle::SessionOptions options;
 * options.errorReporter = errorReporter;
 * auto session = mcp::lifecycle::Session::create(options);
 * @endcode
 */
using ErrorReporter = std::function<void(const ErrorEvent &)>;

/**
 * @brief Helper to safely invoke an error reporter callback.
 *
 * This function wraps the error reporter invocation in a catch-all boundary
 * to ensure that exceptions from the callback never escape.
 *
 * @param reporter The error reporter callback (may be null)
 * @param component The component identifier for the error
 * @param message The error message
 */
inline auto reportError(const ErrorReporter &reporter, std::string_view component, std::string_view message) noexcept -> void
{
  if (!reporter)
  {
    return;
  }

  try
  {
    reporter(ErrorEvent {std::string(component), std::string(message)});
  }
  catch (...)
  {
    // Suppress any exceptions from the error reporter to maintain SDK stability
    detail::suppressReporterException();
  }
}

/**
 * @brief Helper to safely invoke an error reporter callback with exception info.
 *
 * This function extracts information from the current exception (if any) and
 * reports it through the error reporter. Must be called from within a catch block.
 *
 * @param reporter The error reporter callback (may be null)
 * @param component The component identifier for the error
 */
inline auto reportCurrentException(const ErrorReporter &reporter, std::string_view component) noexcept -> void
{
  if (!reporter)
  {
    return;
  }

  try
  {
    std::string message;
    try
    {
      throw;
    }
    catch (const std::exception &error)
    {
      message = error.what();
    }
    catch (...)
    {
      message = "Unknown exception";
    }
    reporter(ErrorEvent {std::string(component), std::move(message)});
  }
  catch (...)
  {
    // Suppress any exceptions from the error reporter to maintain SDK stability
    detail::suppressReporterException();
  }
}

}  // namespace sdk

// Deprecated: Backwards compatibility aliases
using sdk::ErrorEvent;
using sdk::ErrorReporter;
using sdk::reportCurrentException;
using sdk::reportError;

}  // namespace mcp
