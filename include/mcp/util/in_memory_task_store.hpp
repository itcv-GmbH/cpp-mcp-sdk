#pragma once

#include <memory>

#include <mcp/util/in_memory_task_store_options.hpp>
#include <mcp/util/task_store.hpp>

namespace mcp::util
{

class InMemoryTaskStore final : public TaskStore
{
public:
  explicit InMemoryTaskStore(InMemoryTaskStoreOptions options = {});
  ~InMemoryTaskStore() override;

  InMemoryTaskStore(const InMemoryTaskStore &) = delete;
  auto operator=(const InMemoryTaskStore &) -> InMemoryTaskStore & = delete;
  InMemoryTaskStore(InMemoryTaskStore &&) = delete;
  auto operator=(InMemoryTaskStore &&) -> InMemoryTaskStore & = delete;

  auto createTask(TaskCreateOptions options) -> TaskRecordResult override;
  auto getTask(std::string_view taskId, const std::optional<std::string> &authContext) -> TaskRecordResult override;
  auto listTasks(const std::optional<std::string> &authContext) -> std::vector<Task> override;
  auto updateTaskStatus(std::string_view taskId, TaskStatus status, std::optional<std::string> statusMessage, const std::optional<std::string> &authContext)
    -> TaskRecordResult override;
  auto setTaskResult(std::string_view taskId,
                     TaskStatus terminalStatus,
                     std::optional<std::string> statusMessage,
                     jsonrpc::JsonValue result,
                     const std::optional<std::string> &authContext) -> TaskRecordResult override;
  auto setTaskError(std::string_view taskId, TaskStatus terminalStatus, std::optional<std::string> statusMessage, JsonRpcError error, const std::optional<std::string> &authContext)
    -> TaskRecordResult override;
  auto cancelTask(std::string_view taskId, std::optional<std::string> statusMessage, JsonRpcError cancellationError, const std::optional<std::string> &authContext)
    -> TaskRecordResult override;
  auto waitForTaskTerminal(std::string_view taskId, const std::optional<std::string> &authContext) -> TaskTerminalResult override;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace mcp::util
