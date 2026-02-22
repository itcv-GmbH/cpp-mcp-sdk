#pragma once

#include <optional>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/util/task.hpp>

namespace mcp::util
{

struct CreateTaskResult
{
  Task task;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp::util
