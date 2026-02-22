#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/util/create_task_result.hpp>
#include <mcp/util/in_memory_task_store.hpp>
#include <mcp/util/task_augmentation_request.hpp>
#include <mcp/util/task_record_result.hpp>
#include <mcp/util/task_status.hpp>
#include <mcp/util/task_store.hpp>

namespace mcp::util
{

inline constexpr std::int64_t kDefaultTaskPollIntervalMs = 1000;
inline constexpr std::size_t kDefaultTaskListPageSize = 50;

using TaskStatusObserver = std::function<void(const jsonrpc::RequestContext &, const Task &)>;

class TaskReceiver
{
public:
  explicit TaskReceiver(std::shared_ptr<TaskStore> store = std::make_shared<InMemoryTaskStore>(),
                        std::optional<std::int64_t> defaultPollInterval = kDefaultTaskPollIntervalMs,
                        std::size_t listPageSize = kDefaultTaskListPageSize);

  auto setStatusObserver(TaskStatusObserver observer) -> void;

  auto createTask(const jsonrpc::RequestContext &context,
                  const TaskAugmentationRequest &augmentation,
                  std::optional<std::string> statusMessage = std::string("The operation is now in progress.")) -> CreateTaskResult;

  auto updateTaskStatus(const jsonrpc::RequestContext &context, std::string_view taskId, TaskStatus status, std::optional<std::string> statusMessage = std::nullopt)
    -> TaskRecordResult;

  auto completeTaskWithResponse(const jsonrpc::RequestContext &context,
                                std::string_view taskId,
                                const jsonrpc::Response &response,
                                TaskStatus successStatus = TaskStatus::kCompleted,
                                std::optional<std::string> statusMessage = std::nullopt) -> TaskRecordResult;

  auto cancelTask(const jsonrpc::RequestContext &context,
                  std::string_view taskId,
                  std::optional<std::string> statusMessage = std::string("The task was cancelled by request."),
                  std::optional<JsonRpcError> cancellationError = std::nullopt) -> TaskRecordResult;

  auto handleTasksGetRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleTasksResultRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleTasksListRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleTasksCancelRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;

private:
  auto notifyStatusObserver(const jsonrpc::RequestContext &context, const TaskRecordResult &result) -> void;

  std::shared_ptr<TaskStore> store_;
  std::optional<std::int64_t> defaultPollInterval_;
  std::size_t listPageSize_ = kDefaultTaskListPageSize;
  TaskStatusObserver statusObserver_;
};

}  // namespace mcp::util
