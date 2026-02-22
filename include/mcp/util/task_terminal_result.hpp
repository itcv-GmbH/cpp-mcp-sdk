#pragma once

#include <optional>

#include <mcp/errors.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/util/task.hpp>
#include <mcp/util/task_store_error.hpp>

namespace mcp::util
{

struct TaskTerminalResult
{
  TaskStoreError error = TaskStoreError::kNone;
  Task task;
  std::optional<jsonrpc::JsonValue> result;
  std::optional<JsonRpcError> errorPayload;
};

}  // namespace mcp::util
