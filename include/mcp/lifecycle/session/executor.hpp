#pragma once

#include <functional>

namespace mcp
{
namespace lifecycle
{
namespace session
{

/**
 * @brief Abstract executor interface for task scheduling.
 */
class Executor
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

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
