#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/security/limits.hpp>

namespace mcp::util
{

inline constexpr std::string_view kRelatedTaskMetadataKey = "io.modelcontextprotocol/related-task";

inline constexpr std::int64_t kDefaultTaskPollIntervalMs = 1000;
inline constexpr std::size_t kDefaultTaskListPageSize = 50;

enum class TaskStatus : std::uint8_t
{
  kWorking,
  kInputRequired,
  kCompleted,
  kFailed,
  kCancelled,
};

auto toString(TaskStatus status) -> std::string_view;
auto taskStatusFromString(std::string_view status) -> std::optional<TaskStatus>;
auto isTerminalTaskStatus(TaskStatus status) -> bool;
auto isValidTaskStatusTransition(TaskStatus from, TaskStatus targetStatus) -> bool;

struct Task
{
  std::string taskId;
  TaskStatus status = TaskStatus::kWorking;
  std::optional<std::string> statusMessage;
  std::string createdAt;
  std::string lastUpdatedAt;
  std::optional<std::int64_t> ttl;
  std::optional<std::int64_t> pollInterval;
};

struct CreateTaskResult
{
  Task task;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct TaskAugmentationRequest
{
  bool requested = false;
  bool ttlProvided = false;
  std::optional<std::int64_t> ttl;
};

auto parseTaskAugmentation(const std::optional<jsonrpc::JsonValue> &params, std::string *errorMessage = nullptr) -> TaskAugmentationRequest;
auto taskToJson(const Task &task) -> jsonrpc::JsonValue;
auto createTaskResultToJson(const CreateTaskResult &result) -> jsonrpc::JsonValue;
auto injectRelatedTaskMetadata(jsonrpc::JsonValue &resultObject, std::string_view taskId) -> void;
auto authContextForRequest(const jsonrpc::RequestContext &context) -> std::optional<std::string>;

enum class TaskStoreError : std::uint8_t
{
  kNone,
  kNotFound,
  kAccessDenied,
  kInvalidTransition,
  kTerminalImmutable,
  kLimitExceeded,
};

struct TaskRecordResult
{
  TaskStoreError error = TaskStoreError::kNone;
  std::optional<std::string> errorMessage;
  Task task;
};

struct TaskTerminalResult
{
  TaskStoreError error = TaskStoreError::kNone;
  Task task;
  std::optional<jsonrpc::JsonValue> result;
  std::optional<JsonRpcError> errorPayload;
};

struct TaskCreateOptions
{
  std::optional<std::int64_t> ttl;
  std::optional<std::int64_t> pollInterval;
  std::optional<std::string> statusMessage;
  std::optional<std::string> authContext;
};

struct InMemoryTaskStoreOptions
{
  std::int64_t maxTaskTtlMilliseconds = static_cast<std::int64_t>(security::kDefaultMaxTaskTtlMilliseconds);
  std::size_t maxActiveTasksPerAuthContext = security::kDefaultMaxConcurrentTasksPerAuthContext;
};

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
