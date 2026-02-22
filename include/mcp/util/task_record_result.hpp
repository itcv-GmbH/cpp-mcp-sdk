#pragma once

#include <optional>
#include <string>

#include <mcp/util/task.hpp>
#include <mcp/util/task_store_error.hpp>

namespace mcp::util
{

struct TaskRecordResult
{
  TaskStoreError error = TaskStoreError::kNone;
  std::optional<std::string> errorMessage;
  Task task;
};

}  // namespace mcp::util
