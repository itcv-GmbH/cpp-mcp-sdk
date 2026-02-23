#pragma once

#include <memory>

#include <mcp/lifecycle/session/executor.hpp>
#include <mcp/lifecycle/session/handler_threading_policy.hpp>

namespace mcp::lifecycle::session
{

/**
 * @brief Threading configuration for a session.
 */
struct SessionThreading
{
  HandlerThreadingPolicy handlerThreadingPolicy = HandlerThreadingPolicy::kExecutor;
  std::shared_ptr<Executor> handlerExecutor;
};

}  // namespace mcp::lifecycle::session
