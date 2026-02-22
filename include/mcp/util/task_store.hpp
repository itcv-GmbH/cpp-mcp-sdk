#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <mcp/util/task_create_options.hpp>
#include <mcp/util/task_record_result.hpp>
#include <mcp/util/task_terminal_result.hpp>

namespace mcp::util
{

class TaskStore
{
public:
  TaskStore() = default;
  TaskStore(const TaskStore &) = delete;
  TaskStore(TaskStore &&) = delete;
  auto operator=(const TaskStore &) -> TaskStore & = delete;
  auto operator=(TaskStore &&) -> TaskStore & = delete;
  virtual ~TaskStore() = default;

  virtual auto createTask(TaskCreateOptions options) -> TaskRecordResult = 0;
  virtual auto getTask(std::string_view taskId, const std::optional<std::string> &authContext) -> TaskRecordResult = 0;
  virtual auto listTasks(const std::optional<std::string> &authContext) -> std::vector<Task> = 0;

  virtual auto updateTaskStatus(std::string_view taskId, TaskStatus status, std::optional<std::string> statusMessage, const std::optional<std::string> &authContext)
    -> TaskRecordResult = 0;

  virtual auto setTaskResult(std::string_view taskId,
                             TaskStatus terminalStatus,
                             std::optional<std::string> statusMessage,
                             jsonrpc::JsonValue result,
                             const std::optional<std::string> &authContext) -> TaskRecordResult = 0;

  virtual auto setTaskError(std::string_view taskId,
                            TaskStatus terminalStatus,
                            std::optional<std::string> statusMessage,
                            JsonRpcError error,
                            const std::optional<std::string> &authContext) -> TaskRecordResult = 0;

  virtual auto cancelTask(std::string_view taskId, std::optional<std::string> statusMessage, JsonRpcError cancellationError, const std::optional<std::string> &authContext)
    -> TaskRecordResult = 0;

  virtual auto waitForTaskTerminal(std::string_view taskId, const std::optional<std::string> &authContext) -> TaskTerminalResult = 0;
};

}  // namespace mcp::util
