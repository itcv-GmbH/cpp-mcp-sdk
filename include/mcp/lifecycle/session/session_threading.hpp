#pragma once

#include <memory>

#include <mcp/lifecycle/session/executor.hpp>
#include <mcp/lifecycle/session/handler_threading_policy.hpp>

namespace mcp
{
namespace lifecycle
{
namespace session
{

/**
 * @brief Threading configuration for a session.
 */
struct SessionThreading
{
  HandlerThreadingPolicy handlerThreadingPolicy = HandlerThreadingPolicy::kExecutor;
  std::shared_ptr<Executor> handlerExecutor;
};

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
