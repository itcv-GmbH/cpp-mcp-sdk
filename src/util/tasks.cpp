#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/detail/base64url.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/security/crypto_random.hpp>
#include <mcp/util/tasks.hpp>

namespace mcp::util
{
namespace detail
{

constexpr std::string_view kCursorPrefix = "mcp:v1:tasks:";
constexpr std::string_view kTaskGetMethod = "tasks/get";
constexpr std::string_view kTaskResultMethod = "tasks/result";
constexpr std::string_view kTaskListMethod = "tasks/list";
constexpr std::string_view kTaskCancelMethod = "tasks/cancel";
constexpr std::string_view kUnboundAuthContextKey = "__mcp_unbound_auth_context__";

constexpr std::size_t kTaskIdRandomBytes = 24U;

struct PaginationWindow
{
  std::size_t startIndex = 0;
  std::size_t endIndex = 0;
  std::optional<std::string> nextCursor;
};

auto makeInvalidParamsResponse(const jsonrpc::RequestId &requestId, std::string message) -> jsonrpc::Response
{
  return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, std::move(message)), requestId);
}

auto makeInternalError(std::string message) -> JsonRpcError
{
  JsonRpcError error;
  error.code = static_cast<std::int32_t>(JsonRpcErrorCode::kInternalError);
  error.message = std::move(message);
  return error;
}

auto toIso8601Utc(std::chrono::system_clock::time_point timestamp) -> std::string
{
  const std::time_t timeValue = std::chrono::system_clock::to_time_t(timestamp);
  std::tm utcTime {};
  // NOLINTNEXTLINE(readability-use-concise-preprocessor-directives) - _WIN32 is a predefined macro
#if defined(_WIN32)
  gmtime_s(&utcTime, &timeValue);
#else
  gmtime_r(&timeValue, &utcTime);
#endif

  std::ostringstream stream;
  stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

auto encodeCursorPayload(std::string_view payload) -> std::string
{
  return ::mcp::detail::encodeBase64UrlNoPad(payload);
}

auto decodeCursorPayload(std::string_view encoded) -> std::optional<std::string>
{
  return ::mcp::detail::decodeBase64UrlNoPad(encoded);
}

auto makePaginationCursor(std::size_t startIndex) -> std::string
{
  const std::string payload = std::string(kCursorPrefix) + std::to_string(startIndex);
  return encodeCursorPayload(payload);
}

auto parsePaginationCursor(std::string_view cursor) -> std::optional<std::size_t>
{
  if (cursor.empty())
  {
    return std::nullopt;
  }

  const auto decoded = decodeCursorPayload(cursor);
  if (!decoded.has_value())
  {
    return std::nullopt;
  }

  if (decoded->rfind(kCursorPrefix, 0) != 0)
  {
    return std::nullopt;
  }

  const std::string indexText = decoded->substr(kCursorPrefix.size());
  if (indexText.empty())
  {
    return std::nullopt;
  }

  std::size_t consumed = 0;
  std::uint64_t parsedIndex = 0;
  try
  {
    parsedIndex = std::stoull(indexText, &consumed);
  }
  catch (const std::exception &)
  {
    return std::nullopt;
  }

  if (consumed != indexText.size() || parsedIndex > std::numeric_limits<std::size_t>::max())
  {
    return std::nullopt;
  }

  return static_cast<std::size_t>(parsedIndex);
}

auto paginate(std::optional<std::string> cursor, std::size_t totalItems, std::size_t pageSize) -> std::optional<PaginationWindow>
{
  if (pageSize == 0)
  {
    return std::nullopt;
  }

  std::size_t startIndex = 0;
  if (cursor.has_value())
  {
    const auto parsedCursor = parsePaginationCursor(*cursor);
    if (!parsedCursor.has_value() || *parsedCursor > totalItems)
    {
      return std::nullopt;
    }

    startIndex = *parsedCursor;
  }

  const std::size_t endIndex = std::min(startIndex + pageSize, totalItems);

  PaginationWindow window;
  window.startIndex = startIndex;
  window.endIndex = endIndex;
  if (endIndex < totalItems)
  {
    window.nextCursor = makePaginationCursor(endIndex);
  }

  return window;
}

auto parseTaskIdFromRequestParams(const jsonrpc::Request &request, std::string *errorMessage = nullptr) -> std::optional<std::string>
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "Request requires params object with string taskId";
    }
    return std::nullopt;
  }

  const jsonrpc::JsonValue &params = *request.params;
  if (!params.contains("taskId") || !params["taskId"].is_string() || params["taskId"].as<std::string>().empty())
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "Request requires non-empty params.taskId";
    }
    return std::nullopt;
  }

  return params["taskId"].as<std::string>();
}

auto defaultCancellationError(const std::optional<std::string> &statusMessage) -> JsonRpcError
{
  JsonRpcError cancellationError;
  cancellationError.code = static_cast<std::int32_t>(JsonRpcErrorCode::kInvalidRequest);
  cancellationError.message = statusMessage.value_or(std::string("Task cancelled"));
  return cancellationError;
}

auto taskStoreErrorMessage(TaskStoreError error) -> std::string
{
  switch (error)
  {
    case TaskStoreError::kNone:
      return "No task store error";
    case TaskStoreError::kNotFound:
      return "Task not found";
    case TaskStoreError::kAccessDenied:
      return "Task access denied for current auth context";
    case TaskStoreError::kInvalidTransition:
      return "Task status transition is invalid";
    case TaskStoreError::kTerminalImmutable:
      return "Task is already in a terminal status";
    case TaskStoreError::kLimitExceeded:
      return "Task creation rejected by runtime limits";
  }

  return "Unknown task store error";
}

}  // namespace detail

auto toString(TaskStatus status) -> std::string_view
{
  switch (status)
  {
    case TaskStatus::kWorking:
      return "working";
    case TaskStatus::kInputRequired:
      return "input_required";
    case TaskStatus::kCompleted:
      return "completed";
    case TaskStatus::kFailed:
      return "failed";
    case TaskStatus::kCancelled:
      return "cancelled";
  }

  return "working";
}

auto taskStatusFromString(std::string_view status) -> std::optional<TaskStatus>
{
  if (status == "working")
  {
    return TaskStatus::kWorking;
  }
  if (status == "input_required")
  {
    return TaskStatus::kInputRequired;
  }
  if (status == "completed")
  {
    return TaskStatus::kCompleted;
  }
  if (status == "failed")
  {
    return TaskStatus::kFailed;
  }
  if (status == "cancelled")
  {
    return TaskStatus::kCancelled;
  }

  return std::nullopt;
}

auto isTerminalTaskStatus(TaskStatus status) -> bool
{
  return status == TaskStatus::kCompleted || status == TaskStatus::kFailed || status == TaskStatus::kCancelled;
}

auto isValidTaskStatusTransition(TaskStatus from, TaskStatus targetStatus) -> bool
{
  if (isTerminalTaskStatus(from))
  {
    return false;
  }

  if (from == TaskStatus::kWorking)
  {
    return targetStatus == TaskStatus::kInputRequired || targetStatus == TaskStatus::kCompleted || targetStatus == TaskStatus::kFailed || targetStatus == TaskStatus::kCancelled;
  }

  if (from == TaskStatus::kInputRequired)
  {
    return targetStatus == TaskStatus::kWorking || targetStatus == TaskStatus::kCompleted || targetStatus == TaskStatus::kFailed || targetStatus == TaskStatus::kCancelled;
  }

  return false;
}

auto parseTaskAugmentation(const std::optional<jsonrpc::JsonValue> &params, std::string *errorMessage) -> TaskAugmentationRequest
{
  if (!params.has_value())
  {
    return {};
  }

  if (!params->is_object())
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "Request params must be an object";
    }
    return {};
  }

  const jsonrpc::JsonValue &paramsObject = *params;
  if (!paramsObject.contains("task"))
  {
    return {};
  }

  const jsonrpc::JsonValue &taskObject = paramsObject["task"];
  if (!taskObject.is_object())
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "params.task must be an object";
    }
    return {};
  }

  TaskAugmentationRequest augmentation;
  augmentation.requested = true;

  if (!taskObject.contains("ttl"))
  {
    return augmentation;
  }

  augmentation.ttlProvided = true;
  if (taskObject["ttl"].is_null())
  {
    augmentation.ttl = std::nullopt;
    return augmentation;
  }

  std::int64_t ttl = 0;
  if (taskObject["ttl"].is_int64())
  {
    ttl = taskObject["ttl"].as<std::int64_t>();
  }
  else if (taskObject["ttl"].is_uint64())
  {
    const std::uint64_t ttlValue = taskObject["ttl"].as<std::uint64_t>();
    if (ttlValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "params.task.ttl is out of range";
      }
      return {};
    }

    ttl = static_cast<std::int64_t>(ttlValue);
  }
  else
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "params.task.ttl must be an integer or null";
    }
    return {};
  }

  if (ttl < 0)
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "params.task.ttl must be greater than or equal to zero";
    }
    return {};
  }

  augmentation.ttl = ttl;
  return augmentation;
}

auto taskToJson(const Task &task) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue taskJson = jsonrpc::JsonValue::object();
  taskJson["taskId"] = task.taskId;
  taskJson["status"] = std::string(toString(task.status));
  if (task.statusMessage.has_value())
  {
    taskJson["statusMessage"] = *task.statusMessage;
  }
  taskJson["createdAt"] = task.createdAt;
  taskJson["lastUpdatedAt"] = task.lastUpdatedAt;
  if (task.ttl.has_value())
  {
    taskJson["ttl"] = *task.ttl;
  }
  else
  {
    taskJson["ttl"] = jsonrpc::JsonValue::null();
  }

  if (task.pollInterval.has_value())
  {
    taskJson["pollInterval"] = *task.pollInterval;
  }

  return taskJson;
}

auto createTaskResultToJson(const CreateTaskResult &result) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue payload = jsonrpc::JsonValue::object();
  payload["task"] = taskToJson(result.task);
  if (result.metadata.has_value())
  {
    payload["_meta"] = *result.metadata;
  }
  return payload;
}

auto injectRelatedTaskMetadata(jsonrpc::JsonValue &resultObject, std::string_view taskId) -> void
{
  if (!resultObject.is_object())
  {
    return;
  }

  if (!resultObject.contains("_meta") || !resultObject["_meta"].is_object())
  {
    resultObject["_meta"] = jsonrpc::JsonValue::object();
  }

  resultObject["_meta"][std::string(kRelatedTaskMetadataKey)] = jsonrpc::JsonValue::object();
  resultObject["_meta"][std::string(kRelatedTaskMetadataKey)]["taskId"] = std::string(taskId);
}

auto authContextForRequest(const jsonrpc::RequestContext &context) -> std::optional<std::string>
{
  return context.authContext;
}

struct InMemoryTaskStore::Impl
{
  explicit Impl(InMemoryTaskStoreOptions options)
    : options(options)
  {
  }

  struct StoredTask
  {
    Task task;
    std::optional<std::string> authContext;
    std::optional<std::chrono::system_clock::time_point> expiresAt;
    std::optional<jsonrpc::JsonValue> result;
    std::optional<JsonRpcError> errorPayload;
    std::uint64_t sequence = 0;
  };

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static) - Intentionally non-static for testing/mocking
  auto nowSystem() const -> std::chrono::system_clock::time_point { return std::chrono::system_clock::now(); }

  static auto normalizedAuthContextKey(const std::optional<std::string> &authContext) -> std::string { return authContext.value_or(std::string(detail::kUnboundAuthContextKey)); }

  static auto checkAccess(const StoredTask &storedTask, const std::optional<std::string> &authContext) -> bool
  {
    if (!storedTask.authContext.has_value())
    {
      return true;
    }

    return authContext.has_value() && *storedTask.authContext == *authContext;
  }

  auto purgeExpiredLocked() -> void
  {
    const auto now = nowSystem();
    for (auto it = tasks.begin(); it != tasks.end();)
    {
      if (it->second.expiresAt.has_value() && now >= *it->second.expiresAt)
      {
        it = tasks.erase(it);
        continue;
      }

      ++it;
    }
  }

  auto countActiveTasksForAuthContext(const std::optional<std::string> &authContext) const -> std::size_t
  {
    const std::string authContextKey = Impl::normalizedAuthContextKey(authContext);
    std::size_t activeCount = 0;
    for (const auto &entry : tasks)
    {
      if (Impl::normalizedAuthContextKey(entry.second.authContext) != authContextKey)
      {
        continue;
      }

      if (!isTerminalTaskStatus(entry.second.task.status))
      {
        ++activeCount;
      }
    }

    return activeCount;
  }

  static auto generateTaskId() -> std::string
  {
    static constexpr std::string_view hexAlphabet = "0123456789abcdef";
    static constexpr std::uint8_t kHighNibbleMask = 0x0FU;
    static constexpr std::uint8_t kLowNibbleMask = 0x0FU;

    const std::vector<std::uint8_t> randomBytes = security::cryptoRandomBytes(detail::kTaskIdRandomBytes);

    std::string taskId;
    taskId.reserve(detail::kTaskIdRandomBytes * 2U);
    for (const std::uint8_t value : randomBytes)
    {
      // NOLINTNEXTLINE(hicpp-signed-bitwise) - Bitwise ops on uint8_t are well-defined
      taskId.push_back(hexAlphabet[(value >> 4U) & kHighNibbleMask]);
      taskId.push_back(hexAlphabet[value & kLowNibbleMask]);
    }

    return taskId;
  }

  std::mutex mutex;
  std::condition_variable condition;
  std::unordered_map<std::string, StoredTask> tasks;
  std::uint64_t nextSequence = 1;
  InMemoryTaskStoreOptions options;
};

InMemoryTaskStore::InMemoryTaskStore(InMemoryTaskStoreOptions options)
  : impl_(std::make_shared<Impl>(options))
{
}

InMemoryTaskStore::~InMemoryTaskStore() = default;

auto InMemoryTaskStore::createTask(TaskCreateOptions options) -> TaskRecordResult
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->purgeExpiredLocked();

  if (options.ttl.has_value())
  {
    if (*options.ttl < 0)
    {
      return {.error = TaskStoreError::kLimitExceeded, .errorMessage = std::string("Task ttl must be greater than or equal to zero milliseconds")};
    }

    if (*options.ttl > impl_->options.maxTaskTtlMilliseconds)
    {
      return {.error = TaskStoreError::kLimitExceeded,
              .errorMessage = std::string("Task ttl exceeds configured maximum of ") + std::to_string(impl_->options.maxTaskTtlMilliseconds) + " milliseconds"};
    }
  }

  if (impl_->countActiveTasksForAuthContext(options.authContext) >= impl_->options.maxActiveTasksPerAuthContext)
  {
    return {.error = TaskStoreError::kLimitExceeded,
            .errorMessage = std::string("Active task limit reached for auth context (max ") + std::to_string(impl_->options.maxActiveTasksPerAuthContext) + ")"};
  }

  std::string taskId;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while) - Required for collision avoidance loop
  do
  {
    taskId = Impl::generateTaskId();
  } while (impl_->tasks.find(taskId) != impl_->tasks.end());

  const auto now = impl_->nowSystem();

  Impl::StoredTask storedTask;
  storedTask.task.taskId = taskId;
  storedTask.task.status = TaskStatus::kWorking;
  storedTask.task.statusMessage = std::move(options.statusMessage);
  storedTask.task.createdAt = detail::toIso8601Utc(now);
  storedTask.task.lastUpdatedAt = storedTask.task.createdAt;
  storedTask.task.ttl = options.ttl;
  storedTask.task.pollInterval = options.pollInterval;
  storedTask.authContext = std::move(options.authContext);
  storedTask.sequence = impl_->nextSequence++;

  if (options.ttl.has_value())
  {
    storedTask.expiresAt = now + std::chrono::milliseconds(*options.ttl);
  }

  TaskRecordResult result;
  result.task = storedTask.task;
  impl_->tasks.emplace(taskId, std::move(storedTask));
  return result;
}

auto InMemoryTaskStore::getTask(std::string_view taskId, const std::optional<std::string> &authContext) -> TaskRecordResult
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->purgeExpiredLocked();

  const auto taskIter = impl_->tasks.find(std::string(taskId));
  if (taskIter == impl_->tasks.end())
  {
    return {.error = TaskStoreError::kNotFound};
  }

  if (!Impl::checkAccess(taskIter->second, authContext))
  {
    return {.error = TaskStoreError::kAccessDenied};
  }

  return {.error = TaskStoreError::kNone, .task = taskIter->second.task};
}

auto InMemoryTaskStore::listTasks(const std::optional<std::string> &authContext) -> std::vector<Task>
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->purgeExpiredLocked();

  std::vector<std::pair<std::uint64_t, Task>> orderedTasks;
  orderedTasks.reserve(impl_->tasks.size());
  for (const auto &entry : impl_->tasks)
  {
    if (!Impl::checkAccess(entry.second, authContext))
    {
      continue;
    }

    orderedTasks.emplace_back(entry.second.sequence, entry.second.task);
  }

  std::sort(orderedTasks.begin(), orderedTasks.end(), [](const auto &left, const auto &right) -> bool { return left.first < right.first; });

  std::vector<Task> tasks;
  tasks.reserve(orderedTasks.size());
  for (auto &entry : orderedTasks)
  {
    tasks.push_back(std::move(entry.second));
  }

  return tasks;
}

auto InMemoryTaskStore::updateTaskStatus(std::string_view taskId, TaskStatus status, std::optional<std::string> statusMessage, const std::optional<std::string> &authContext)
  -> TaskRecordResult
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->purgeExpiredLocked();

  const auto taskIter = impl_->tasks.find(std::string(taskId));
  if (taskIter == impl_->tasks.end())
  {
    return {.error = TaskStoreError::kNotFound};
  }

  if (!Impl::checkAccess(taskIter->second, authContext))
  {
    return {.error = TaskStoreError::kAccessDenied};
  }

  if (isTerminalTaskStatus(taskIter->second.task.status))
  {
    return {.error = TaskStoreError::kTerminalImmutable, .task = taskIter->second.task};
  }

  if (!isValidTaskStatusTransition(taskIter->second.task.status, status))
  {
    return {.error = TaskStoreError::kInvalidTransition, .task = taskIter->second.task};
  }

  taskIter->second.task.status = status;
  taskIter->second.task.statusMessage = std::move(statusMessage);
  taskIter->second.task.lastUpdatedAt = detail::toIso8601Utc(impl_->nowSystem());

  TaskRecordResult result {.error = TaskStoreError::kNone, .task = taskIter->second.task};
  impl_->condition.notify_all();
  return result;
}

auto InMemoryTaskStore::setTaskResult(std::string_view taskId,
                                      TaskStatus terminalStatus,
                                      std::optional<std::string> statusMessage,
                                      jsonrpc::JsonValue result,
                                      const std::optional<std::string> &authContext) -> TaskRecordResult
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->purgeExpiredLocked();

  const auto taskIter = impl_->tasks.find(std::string(taskId));
  if (taskIter == impl_->tasks.end())
  {
    return {.error = TaskStoreError::kNotFound};
  }

  if (!Impl::checkAccess(taskIter->second, authContext))
  {
    return {.error = TaskStoreError::kAccessDenied};
  }

  if (isTerminalTaskStatus(taskIter->second.task.status))
  {
    return {.error = TaskStoreError::kTerminalImmutable, .task = taskIter->second.task};
  }

  if (!isTerminalTaskStatus(terminalStatus) || !isValidTaskStatusTransition(taskIter->second.task.status, terminalStatus))
  {
    return {.error = TaskStoreError::kInvalidTransition, .task = taskIter->second.task};
  }

  taskIter->second.task.status = terminalStatus;
  taskIter->second.task.statusMessage = std::move(statusMessage);
  taskIter->second.task.lastUpdatedAt = detail::toIso8601Utc(impl_->nowSystem());
  taskIter->second.result = std::move(result);
  taskIter->second.errorPayload.reset();

  TaskRecordResult mutationResult {.error = TaskStoreError::kNone, .task = taskIter->second.task};
  impl_->condition.notify_all();
  return mutationResult;
}

auto InMemoryTaskStore::setTaskError(std::string_view taskId,
                                     TaskStatus terminalStatus,
                                     std::optional<std::string> statusMessage,
                                     JsonRpcError error,
                                     const std::optional<std::string> &authContext) -> TaskRecordResult
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->purgeExpiredLocked();

  const auto taskIter = impl_->tasks.find(std::string(taskId));
  if (taskIter == impl_->tasks.end())
  {
    return {.error = TaskStoreError::kNotFound};
  }

  if (!Impl::checkAccess(taskIter->second, authContext))
  {
    return {.error = TaskStoreError::kAccessDenied};
  }

  if (isTerminalTaskStatus(taskIter->second.task.status))
  {
    return {.error = TaskStoreError::kTerminalImmutable, .task = taskIter->second.task};
  }

  if (!isTerminalTaskStatus(terminalStatus) || !isValidTaskStatusTransition(taskIter->second.task.status, terminalStatus))
  {
    return {.error = TaskStoreError::kInvalidTransition, .task = taskIter->second.task};
  }

  taskIter->second.task.status = terminalStatus;
  taskIter->second.task.statusMessage = std::move(statusMessage);
  taskIter->second.task.lastUpdatedAt = detail::toIso8601Utc(impl_->nowSystem());
  taskIter->second.errorPayload = std::move(error);
  taskIter->second.result.reset();

  TaskRecordResult mutationResult {.error = TaskStoreError::kNone, .task = taskIter->second.task};
  impl_->condition.notify_all();
  return mutationResult;
}

auto InMemoryTaskStore::cancelTask(std::string_view taskId, std::optional<std::string> statusMessage, JsonRpcError cancellationError, const std::optional<std::string> &authContext)
  -> TaskRecordResult
{
  return setTaskError(taskId, TaskStatus::kCancelled, std::move(statusMessage), std::move(cancellationError), authContext);
}

auto InMemoryTaskStore::waitForTaskTerminal(std::string_view taskId, const std::optional<std::string> &authContext) -> TaskTerminalResult
{
  std::unique_lock<std::mutex> lock(impl_->mutex);

  while (true)
  {
    impl_->purgeExpiredLocked();

    const auto taskIter = impl_->tasks.find(std::string(taskId));
    if (taskIter == impl_->tasks.end())
    {
      return {.error = TaskStoreError::kNotFound};
    }

    if (!Impl::checkAccess(taskIter->second, authContext))
    {
      return {.error = TaskStoreError::kAccessDenied};
    }

    if (isTerminalTaskStatus(taskIter->second.task.status))
    {
      TaskTerminalResult terminalResult;
      terminalResult.error = TaskStoreError::kNone;
      terminalResult.task = taskIter->second.task;
      terminalResult.result = taskIter->second.result;
      terminalResult.errorPayload = taskIter->second.errorPayload;

      if (!terminalResult.result.has_value() && !terminalResult.errorPayload.has_value())
      {
        terminalResult.errorPayload = detail::makeInternalError("Task reached a terminal state without a stored result payload");
      }

      return terminalResult;
    }

    if (taskIter->second.expiresAt.has_value())
    {
      impl_->condition.wait_until(lock, *taskIter->second.expiresAt);
    }
    else
    {
      impl_->condition.wait(lock);
    }
  }
}

TaskReceiver::TaskReceiver(std::shared_ptr<TaskStore> store, std::optional<std::int64_t> defaultPollInterval, std::size_t listPageSize)
  : store_(std::move(store))
  , defaultPollInterval_(defaultPollInterval)
  , listPageSize_(listPageSize)
{
  if (!store_)
  {
    throw std::invalid_argument("TaskReceiver requires a non-null TaskStore");
  }
}

auto TaskReceiver::setStatusObserver(TaskStatusObserver observer) -> void
{
  statusObserver_ = std::move(observer);
}

auto TaskReceiver::createTask(const jsonrpc::RequestContext &context, const TaskAugmentationRequest &augmentation, std::optional<std::string> statusMessage) -> CreateTaskResult
{
  TaskCreateOptions createOptions;
  if (augmentation.ttlProvided)
  {
    createOptions.ttl = augmentation.ttl;
  }
  createOptions.pollInterval = defaultPollInterval_;
  createOptions.statusMessage = std::move(statusMessage);
  createOptions.authContext = authContextForRequest(context);

  const TaskRecordResult createResult = store_->createTask(std::move(createOptions));
  if (createResult.error != TaskStoreError::kNone)
  {
    if (createResult.errorMessage.has_value())
    {
      throw std::runtime_error(*createResult.errorMessage);
    }

    throw std::runtime_error(detail::taskStoreErrorMessage(createResult.error));
  }

  CreateTaskResult response;
  response.task = createResult.task;
  return response;
}

auto TaskReceiver::updateTaskStatus(const jsonrpc::RequestContext &context, std::string_view taskId, TaskStatus status, std::optional<std::string> statusMessage)
  -> TaskRecordResult
{
  const TaskRecordResult result = store_->updateTaskStatus(taskId, status, std::move(statusMessage), authContextForRequest(context));
  notifyStatusObserver(context, result);
  return result;
}

auto TaskReceiver::completeTaskWithResponse(const jsonrpc::RequestContext &context,
                                            std::string_view taskId,
                                            const jsonrpc::Response &response,
                                            TaskStatus successStatus,
                                            std::optional<std::string> statusMessage) -> TaskRecordResult
{
  TaskRecordResult mutationResult;
  if (std::holds_alternative<jsonrpc::SuccessResponse>(response))
  {
    const auto &successResponse = std::get<jsonrpc::SuccessResponse>(response);
    mutationResult = store_->setTaskResult(taskId, successStatus, std::move(statusMessage), successResponse.result, authContextForRequest(context));
  }
  else
  {
    const auto &errorResponse = std::get<jsonrpc::ErrorResponse>(response);
    mutationResult = store_->setTaskError(taskId, TaskStatus::kFailed, std::move(statusMessage), errorResponse.error, authContextForRequest(context));
  }

  notifyStatusObserver(context, mutationResult);
  return mutationResult;
}

auto TaskReceiver::cancelTask(const jsonrpc::RequestContext &context,
                              std::string_view taskId,
                              // NOLINTNEXTLINE(performance-unnecessary-value-param)
                              std::optional<std::string> statusMessage,
                              // NOLINTNEXTLINE(performance-unnecessary-value-param)
                              std::optional<JsonRpcError> cancellationError) -> TaskRecordResult
{
  const JsonRpcError effectiveError = cancellationError.value_or(detail::defaultCancellationError(statusMessage));
  const TaskRecordResult result = store_->cancelTask(taskId, std::move(statusMessage), effectiveError, authContextForRequest(context));
  notifyStatusObserver(context, result);
  return result;
}

auto TaskReceiver::handleTasksGetRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  std::string parseError;
  const auto taskId = detail::parseTaskIdFromRequestParams(request, &parseError);
  if (!taskId.has_value())
  {
    return detail::makeInvalidParamsResponse(request.id, parseError);
  }

  const TaskRecordResult taskResult = store_->getTask(*taskId, authContextForRequest(context));
  if (taskResult.error != TaskStoreError::kNone)
  {
    return detail::makeInvalidParamsResponse(request.id, "Failed to retrieve task: Task not found");
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = taskToJson(taskResult.task);
  return response;
}

auto TaskReceiver::handleTasksResultRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  std::string parseError;
  const auto taskId = detail::parseTaskIdFromRequestParams(request, &parseError);
  if (!taskId.has_value())
  {
    return detail::makeInvalidParamsResponse(request.id, parseError);
  }

  const TaskTerminalResult terminalResult = store_->waitForTaskTerminal(*taskId, authContextForRequest(context));
  if (terminalResult.error != TaskStoreError::kNone)
  {
    return detail::makeInvalidParamsResponse(request.id, "Failed to retrieve task: Task not found");
  }

  if (terminalResult.result.has_value())
  {
    jsonrpc::SuccessResponse response;
    response.id = request.id;
    response.result = *terminalResult.result;
    injectRelatedTaskMetadata(response.result, *taskId);
    return response;
  }

  if (terminalResult.errorPayload.has_value())
  {
    return jsonrpc::makeErrorResponse(*terminalResult.errorPayload, request.id);
  }

  return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Task terminal result is missing payload"), request.id);
}

auto TaskReceiver::handleTasksListRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  std::optional<std::string> cursor;
  if (request.params.has_value())
  {
    if (!request.params->is_object())
    {
      return detail::makeInvalidParamsResponse(request.id, "tasks/list requires params to be an object when provided");
    }

    const jsonrpc::JsonValue &params = *request.params;
    if (params.contains("cursor"))
    {
      if (!params["cursor"].is_string())
      {
        return detail::makeInvalidParamsResponse(request.id, "tasks/list requires params.cursor to be a string");
      }

      cursor = params["cursor"].as<std::string>();
    }
  }

  const std::vector<Task> tasks = store_->listTasks(authContextForRequest(context));
  const auto pagination = detail::paginate(cursor, tasks.size(), listPageSize_);
  if (!pagination.has_value())
  {
    return detail::makeInvalidParamsResponse(request.id, "Invalid tasks/list cursor");
  }

  jsonrpc::JsonValue tasksJson = jsonrpc::JsonValue::array();
  for (std::size_t index = pagination->startIndex; index < pagination->endIndex; ++index)
  {
    tasksJson.push_back(taskToJson(tasks[index]));
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["tasks"] = std::move(tasksJson);
  if (pagination->nextCursor.has_value())
  {
    response.result["nextCursor"] = *pagination->nextCursor;
  }

  return response;
}

auto TaskReceiver::handleTasksCancelRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  std::string parseError;
  const auto taskId = detail::parseTaskIdFromRequestParams(request, &parseError);
  if (!taskId.has_value())
  {
    return detail::makeInvalidParamsResponse(request.id, parseError);
  }

  const TaskRecordResult cancellationResult = cancelTask(context, *taskId);
  if (cancellationResult.error == TaskStoreError::kTerminalImmutable)
  {
    return detail::makeInvalidParamsResponse(request.id, "Cannot cancel task: already in terminal status '" + std::string(toString(cancellationResult.task.status)) + "'");
  }

  if (cancellationResult.error != TaskStoreError::kNone)
  {
    return detail::makeInvalidParamsResponse(request.id, "Failed to cancel task: Task not found");
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = taskToJson(cancellationResult.task);
  return response;
}

auto TaskReceiver::notifyStatusObserver(const jsonrpc::RequestContext &context, const TaskRecordResult &result) -> void
{
  if (!statusObserver_ || result.error != TaskStoreError::kNone)
  {
    return;
  }

  statusObserver_(context, result.task);
}

}  // namespace mcp::util
