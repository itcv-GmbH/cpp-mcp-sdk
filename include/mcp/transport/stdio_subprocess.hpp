#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/export.hpp>
#include <mcp/transport/stdio_subprocess_shutdown_options.hpp>

namespace mcp::transport
{

/**
 * @brief STDIO subprocess handle for managing external MCP servers.
 *
 * @section Thread Safety
 *
 * @par StdioSubprocess - Thread-compatible
 * - Designed for single-threaded or externally synchronized use
 * - Methods that modify state (writeLine(), readLine(), closeStdin(), shutdown())
 *   must not be called concurrently
 * - Query methods (valid(), isRunning(), exitCode(), capturedStderr()) are thread-safe
 *
 * @par Concurrency Rules:
 * - External synchronization is required if accessed from multiple threads concurrently.
 *
 * @section Exceptions
 *
 * - Constructor: Does not throw
 * - Destructor: noexcept (guaranteed not to throw)
 * - Move operations: noexcept
 * - valid() noexcept
 * - writeLine(): Throws std::runtime_error on write failure or broken pipe
 * - readLine(): Returns bool success, may throw std::runtime_error on I/O error
 * - closeStdin() noexcept
 * - waitForExit(): Returns bool
 * - shutdown() noexcept: Returns bool success/failure
 * - isRunning(): Returns bool
 * - exitCode() const: Returns std::optional<int>
 * - capturedStderr() const: Returns std::string
 */
class MCP_SDK_EXPORT StdioSubprocess final
{
public:
  StdioSubprocess();
  ~StdioSubprocess();

  StdioSubprocess(const StdioSubprocess &) = delete;
  auto operator=(const StdioSubprocess &) -> StdioSubprocess & = delete;
  StdioSubprocess(StdioSubprocess &&other) noexcept;
  auto operator=(StdioSubprocess &&other) noexcept -> StdioSubprocess &;

  [[nodiscard]] auto valid() const noexcept -> bool;
  auto writeLine(std::string_view line) -> void;
  auto readLine(std::string &line) -> bool;
  auto closeStdin() noexcept -> void;
  [[nodiscard]] auto waitForExit(std::chrono::milliseconds timeout) -> bool;
  [[nodiscard]] auto shutdown(StdioSubprocessShutdownOptions options = {}) noexcept -> bool;
  [[nodiscard]] auto isRunning() -> bool;
  [[nodiscard]] auto exitCode() const -> std::optional<int>;
  [[nodiscard]] auto capturedStderr() const -> std::string;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit StdioSubprocess(std::unique_ptr<Impl> impl);
  friend class StdioTransport;
};

}  // namespace mcp::transport
