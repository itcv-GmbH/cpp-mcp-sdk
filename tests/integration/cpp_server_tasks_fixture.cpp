#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/util/tasks.hpp>

namespace
{

struct Options
{
  std::string bindAddress = "127.0.0.1";
  std::uint16_t port = 0;
  std::string path = "/mcp";
};

constexpr std::string_view kStableTaskId = "deterministic-long-task";

struct TaskState
{
  std::string id;
  std::string status;  // "working", "completed", "failed", "cancelled"
  mcp::jsonrpc::JsonValue result;
  std::chrono::steady_clock::time_point createdAt;
  std::chrono::steady_clock::time_point lastUpdatedAt;
  std::atomic<bool> cancelled {false};
  std::promise<mcp::jsonrpc::JsonValue> resultPromise;
};

// Global task storage and management
std::mutex tasksMutex;
std::unordered_map<std::string, std::shared_ptr<TaskState>> tasks;
std::atomic<int> nextTaskId {1};

// Shared server reference for sending notifications
std::shared_ptr<mcp::Server> g_server;

auto parsePort(const std::string &value) -> std::uint16_t
{
  const auto parsed = static_cast<std::uint64_t>(std::strtoull(value.c_str(), nullptr, 10));  // NOLINT(cert-err34-c)
  if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()))
  {
    throw std::invalid_argument("port must be <= 65535");
  }

  return static_cast<std::uint16_t>(parsed);
}

auto parseOptions(int argc, char **argv) -> Options
{
  Options options;

  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
  for (int index = 1; index < argc; ++index)
  {
    arguments.emplace_back(argv[index]);
  }

  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string_view argument = arguments[index];
    const auto requireValue = [&arguments, &index](std::string_view name) -> std::string
    {
      if (index + 1 >= arguments.size())
      {
        throw std::invalid_argument("Missing value for argument: " + std::string(name));
      }

      ++index;
      return arguments[index];
    };

    if (argument == "--bind")
    {
      options.bindAddress = requireValue(argument);
      continue;
    }

    if (argument == "--port")
    {
      options.port = parsePort(requireValue(argument));
      continue;
    }

    if (argument == "--path")
    {
      options.path = requireValue(argument);
      continue;
    }

    throw std::invalid_argument("Unknown argument: " + std::string(argument));
  }

  return options;
}

auto makeTextContent(const std::string &text) -> mcp::jsonrpc::JsonValue
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = text;
  return content;
}

auto sendStatusNotification(const std::string &taskId, const std::string &status, const std::string &statusMessage) -> void
{
  if (!g_server)
  {
    return;
  }

  try
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/tasks/status";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["taskId"] = taskId;
    (*notification.params)["status"] = status;
    (*notification.params)["statusMessage"] = statusMessage;

    mcp::jsonrpc::RequestContext context;
    g_server->sendNotification(context, std::move(notification));
  }
  catch (const std::exception &)
  {
    // Ignore notification errors
  }
}

auto sendProgressNotification(const std::string &taskId, int progress, int total, const std::string &message) -> void
{
  if (!g_server)
  {
    return;
  }

  try
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/progress";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["progressToken"] = taskId;
    (*notification.params)["progress"] = progress;
    (*notification.params)["total"] = total;
    (*notification.params)["message"] = message;

    mcp::jsonrpc::RequestContext context;
    g_server->sendNotification(context, std::move(notification));
  }
  catch (const std::exception &)
  {
    // Ignore notification errors
  }
}

auto sendCancelledNotification(const std::string &taskId) -> void
{
  if (!g_server)
  {
    return;
  }

  try
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["taskId"] = taskId;
    (*notification.params)["status"] = "cancelled";

    mcp::jsonrpc::RequestContext context;
    g_server->sendNotification(context, std::move(notification));
  }
  catch (const std::exception &)
  {
    // Ignore notification errors
  }
}

void runLongTask(const std::string &taskId, int durationSeconds)
{
  const int totalSteps = durationSeconds;
  for (int step = 0; step <= totalSteps; ++step)
  {
    // Check for cancellation
    std::shared_ptr<TaskState> taskState;
    {
      std::scoped_lock lock(tasksMutex);
      auto it = tasks.find(taskId);
      if (it != tasks.end())
      {
        taskState = it->second;
      }
    }

    if (taskState && taskState->cancelled.load())
    {
      taskState->status = "cancelled";
      taskState->lastUpdatedAt = std::chrono::steady_clock::now();
      sendCancelledNotification(taskId);
      return;
    }

    // Emit progress notification at deterministic checkpoints
    if (step == 0 || step == totalSteps || step == totalSteps / 2)
    {
      sendProgressNotification(taskId, step, totalSteps, "Step " + std::to_string(step) + " of " + std::to_string(totalSteps));
    }

    // Emit status notification at start and end
    if (step == 0)
    {
      sendStatusNotification(taskId, "working", "Task started");
    }
    else if (step == totalSteps)
    {
      sendStatusNotification(taskId, "completed", "Task completed successfully");
    }
    else
    {
      sendStatusNotification(taskId, "working", "Task in progress: " + std::to_string(step) + "/" + std::to_string(totalSteps));
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Task completed successfully
  std::shared_ptr<TaskState> taskState;
  {
    std::scoped_lock lock(tasksMutex);
    auto it = tasks.find(taskId);
    if (it != tasks.end())
    {
      taskState = it->second;
    }
  }

  if (taskState)
  {
    taskState->status = "completed";
    taskState->lastUpdatedAt = std::chrono::steady_clock::now();

    // Build result
    mcp::jsonrpc::JsonValue taskResult = mcp::jsonrpc::JsonValue::object();
    taskResult["taskId"] = taskId;
    taskResult["content"] = mcp::jsonrpc::JsonValue::array();
    mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
    content["type"] = "text";
    content["text"] = "Task completed: " + taskId;
    taskResult["content"].push_back(std::move(content));

    taskState->result = taskResult;
    taskState->resultPromise.set_value(taskResult);
  }
}

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

    auto makeServer = [&options]() -> std::shared_ptr<mcp::Server>
    {
      mcp::ToolsCapability toolsCapability;
      mcp::ResourcesCapability resourcesCapability;
      mcp::PromptsCapability promptsCapability;

      mcp::TasksCapability tasksCapability;
      tasksCapability.list = true;
      tasksCapability.cancel = true;
      tasksCapability.toolsCall = true;

      mcp::ServerConfiguration configuration;
      configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, tasksCapability, std::nullopt);
      configuration.serverInfo = mcp::Implementation("cpp-integration-server-tasks", "1.0.0");
      configuration.instructions = "Integration fixture server for reference SDK tasks tests.";
      configuration.emitTaskStatusNotifications = true;

      // Use built-in in-memory task store
      configuration.taskStore = std::make_shared<mcp::util::InMemoryTaskStore>();
      configuration.defaultTaskPollInterval = 100;  // Poll every 100ms for faster tests

      const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

      // Store global reference for notifications
      g_server = server;

      // Register tool to start a deterministic long-running task
      mcp::ToolDefinition longTaskTool;
      longTaskTool.name = "cpp_start_long_task";
      longTaskTool.description = "Start a deterministic long-running task that takes 5 seconds";
      longTaskTool.inputSchema = mcp::jsonrpc::JsonValue::object();
      longTaskTool.inputSchema["type"] = "object";
      longTaskTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
      longTaskTool.inputSchema["properties"]["duration"] = mcp::jsonrpc::JsonValue::object();
      longTaskTool.inputSchema["properties"]["duration"]["type"] = "integer";
      longTaskTool.inputSchema["properties"]["duration"]["description"] = "Duration in seconds";
      longTaskTool.inputSchema["properties"]["duration"]["default"] = 5;
      longTaskTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();

      server->registerTool(std::move(longTaskTool),
                           [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                           {
                             mcp::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();

                             // Default to 5 seconds if not specified
                             int duration = 5;
                             if (context.arguments.contains("duration"))
                             {
                               duration = static_cast<int>(context.arguments["duration"].as<std::int64_t>());
                             }

                             // Generate a deterministic task ID based on nextTaskId
                             std::string taskId = std::string(kStableTaskId) + "-" + std::to_string(nextTaskId.fetch_add(1));
                             if (duration == 5 && nextTaskId.load() == 1)
                             {
                               // First task with default duration gets the stable ID
                               taskId = std::string(kStableTaskId);
                             }

                             // Create task state
                             auto taskState = std::make_shared<TaskState>();
                             taskState->id = taskId;
                             taskState->status = "working";
                             taskState->createdAt = std::chrono::steady_clock::now();
                             taskState->lastUpdatedAt = taskState->createdAt;
                             taskState->result = mcp::jsonrpc::JsonValue::null();

                             // Store task
                             {
                               std::scoped_lock lock(tasksMutex);
                               tasks[taskId] = taskState;
                             }

                             result.content.push_back(makeTextContent("Started task: " + taskId + " with duration " + std::to_string(duration) + " seconds"));

                             // Start background thread to run the task
                             std::thread worker([taskId, duration]() { runLongTask(taskId, duration); });
                             worker.detach();

                             return result;
                           });

      // Register task handlers
      server->registerRequestHandler("tasks/list",
                                     [](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                     {
                                       return std::async(std::launch::deferred,
                                                         [&context, &request]() -> mcp::jsonrpc::Response
                                                         {
                                                           mcp::jsonrpc::Response response;
                                                           mcp::jsonrpc::SuccessResponse success;
                                                           success.result = mcp::jsonrpc::JsonValue::object();
                                                           success.result["tasks"] = mcp::jsonrpc::JsonValue::array();

                                                           // List tasks from our internal storage
                                                           std::scoped_lock lock(tasksMutex);
                                                           for (const auto &[taskId, taskState] : tasks)
                                                           {
                                                             mcp::jsonrpc::JsonValue taskInfo = mcp::jsonrpc::JsonValue::object();
                                                             taskInfo["taskId"] = taskState->id;
                                                             taskInfo["status"] = taskState->status;
                                                             success.result["tasks"].push_back(std::move(taskInfo));
                                                           }

                                                           response = std::move(success);
                                                           return response;
                                                         });
                                     });

      server->registerRequestHandler("tasks/get",
                                     [](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                     {
                                       return std::async(std::launch::deferred,
                                                         [&context, &request]() -> mcp::jsonrpc::Response
                                                         {
                                                           if (!request.params || !request.params->is_object() || !request.params->contains("taskId"))
                                                           {
                                                             mcp::jsonrpc::Response response;
                                                             mcp::jsonrpc::ErrorResponse error;
                                                             error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
                                                             error.error.message = "Missing taskId parameter";
                                                             response = std::move(error);
                                                             return response;
                                                           }

                                                           std::string taskId = (*request.params)["taskId"].as<std::string>();

                                                           std::shared_ptr<TaskState> taskState;
                                                           {
                                                             std::scoped_lock lock(tasksMutex);
                                                             auto it = tasks.find(taskId);
                                                             if (it == tasks.end())
                                                             {
                                                               mcp::jsonrpc::Response response;
                                                               mcp::jsonrpc::ErrorResponse error;
                                                               error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
                                                               error.error.message = "Task not found: " + taskId;
                                                               response = std::move(error);
                                                               return response;
                                                             }
                                                             taskState = it->second;
                                                           }

                                                           mcp::jsonrpc::Response response;
                                                           mcp::jsonrpc::SuccessResponse success;
                                                           success.result = mcp::jsonrpc::JsonValue::object();
                                                           success.result["task"] = mcp::jsonrpc::JsonValue::object();
                                                           success.result["task"]["taskId"] = taskState->id;
                                                           success.result["task"]["status"] = taskState->status;
                                                           response = std::move(success);
                                                           return response;
                                                         });
                                     });

      server->registerRequestHandler("tasks/result",
                                     [](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                     {
                                       return std::async(std::launch::deferred,
                                                         [&context, &request]() -> mcp::jsonrpc::Response
                                                         {
                                                           if (!request.params || !request.params->is_object() || !request.params->contains("taskId"))
                                                           {
                                                             mcp::jsonrpc::Response response;
                                                             mcp::jsonrpc::ErrorResponse error;
                                                             error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
                                                             error.error.message = "Missing taskId parameter";
                                                             response = std::move(error);
                                                             return response;
                                                           }

                                                           std::string taskId = (*request.params)["taskId"].as<std::string>();

                                                           std::shared_ptr<TaskState> taskState;
                                                           {
                                                             std::scoped_lock lock(tasksMutex);
                                                             auto it = tasks.find(taskId);
                                                             if (it == tasks.end())
                                                             {
                                                               mcp::jsonrpc::Response response;
                                                               mcp::jsonrpc::ErrorResponse error;
                                                               error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
                                                               error.error.message = "Task not found: " + taskId;
                                                               response = std::move(error);
                                                               return response;
                                                             }
                                                             taskState = it->second;
                                                           }

                                                           // Wait for result with timeout
                                                           auto future = taskState->resultPromise.get_future();
                                                           auto status = future.wait_for(std::chrono::seconds(30));

                                                           if (status != std::future_status::ready)
                                                           {
                                                             mcp::jsonrpc::Response response;
                                                             mcp::jsonrpc::ErrorResponse error;
                                                             error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError);
                                                             error.error.message = "Task did not complete within timeout";
                                                             response = std::move(error);
                                                             return response;
                                                           }

                                                           mcp::jsonrpc::Response response;
                                                           mcp::jsonrpc::SuccessResponse success;
                                                           success.result = future.get();
                                                           response = std::move(success);
                                                           return response;
                                                         });
                                     });

      server->registerRequestHandler("tasks/cancel",
                                     [](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                     {
                                       return std::async(std::launch::deferred,
                                                         [&context, &request]() -> mcp::jsonrpc::Response
                                                         {
                                                           if (!request.params || !request.params->is_object() || !request.params->contains("taskId"))
                                                           {
                                                             mcp::jsonrpc::Response response;
                                                             mcp::jsonrpc::ErrorResponse error;
                                                             error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
                                                             error.error.message = "Missing taskId parameter";
                                                             response = std::move(error);
                                                             return response;
                                                           }

                                                           std::string taskId = (*request.params)["taskId"].as<std::string>();

                                                           std::shared_ptr<TaskState> taskState;
                                                           {
                                                             std::scoped_lock lock(tasksMutex);
                                                             auto it = tasks.find(taskId);
                                                             if (it == tasks.end())
                                                             {
                                                               mcp::jsonrpc::Response response;
                                                               mcp::jsonrpc::ErrorResponse error;
                                                               error.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
                                                               error.error.message = "Task not found: " + taskId;
                                                               response = std::move(error);
                                                               return response;
                                                             }
                                                             taskState = it->second;
                                                           }

                                                           // Cancel the task
                                                           taskState->cancelled.store(true);
                                                           taskState->status = "cancelled";
                                                           taskState->lastUpdatedAt = std::chrono::steady_clock::now();

                                                           // Emit cancelled notification
                                                           sendCancelledNotification(taskId);

                                                           mcp::jsonrpc::Response response;
                                                           mcp::jsonrpc::SuccessResponse success;
                                                           success.result = mcp::jsonrpc::JsonValue::object();
                                                           success.result["task"] = mcp::jsonrpc::JsonValue::object();
                                                           success.result["task"]["taskId"] = taskId;
                                                           success.result["task"]["status"] = "cancelled";
                                                           response = std::move(success);
                                                           return response;
                                                         });
                                     });

      return server;
    };

    mcp::StreamableHttpServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.http.endpoint.bindAddress = options.bindAddress;
    runnerOptions.transportOptions.http.endpoint.bindLocalhostOnly = true;
    runnerOptions.transportOptions.http.endpoint.port = options.port;
    runnerOptions.transportOptions.http.endpoint.path = options.path;
    runnerOptions.transportOptions.http.requireSessionId = false;

    mcp::StreamableHttpServerRunner runner(makeServer, std::move(runnerOptions));
    runner.start();

    std::cout << "cpp integration server listening on http://" << options.bindAddress << ":" << runner.localPort() << options.path << '\n';
    std::cout.flush();

    std::string ignoredLine;
    while (std::getline(std::cin, ignoredLine))
    {
    }

    runner.stop();

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_server_tasks_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
