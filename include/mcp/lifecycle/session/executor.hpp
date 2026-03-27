#pragma once

#include <functional>

#include <mcp/export.hpp>

namespace mcp::lifecycle::session
{

/**
 * @brief Abstract executor interface for task scheduling.
 */
class MCP_SDK_EXPORT Executor
{
public:
  virtual ~Executor() = default;
  Executor() = default;
  Executor(const Executor &) = delete;
  Executor(Executor &&) = delete;
  auto operator=(const Executor &) -> Executor & = delete;
  auto operator=(Executor &&) -> Executor & = delete;
  virtual auto post(std::function<void()> task) -> void = 0;
};

}  // namespace mcp::lifecycle::session
