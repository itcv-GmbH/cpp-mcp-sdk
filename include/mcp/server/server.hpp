#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/lifecycle/session.hpp>

namespace mcp
{

struct ServerConfiguration
{
  SessionOptions sessionOptions;
  ServerCapabilities capabilities;
  std::optional<Implementation> serverInfo;
  std::optional<std::string> instructions;
};

enum class LogLevel
{
  kDebug,
  kInfo,
  kNotice,
  kWarning,
  kError,
  kCritical,
  kAlert,
  kEmergency,
};

enum class CompletionReferenceType
{
  kPrompt,
  kResource,
};

struct CompletionRequest
{
  CompletionReferenceType referenceType = CompletionReferenceType::kPrompt;
  std::string referenceValue;
  std::string argumentName;
  std::string argumentValue;
  std::optional<jsonrpc::JsonValue> contextArguments;
};

struct CompletionResult
{
  std::vector<std::string> values;
  std::optional<std::size_t> total;
  std::optional<bool> hasMore;
};

enum class ListEndpoint
{
  kTools,
  kResources,
  kPrompts,
  kTasks,
};

struct PaginationWindow
{
  std::size_t startIndex = 0;
  std::size_t endIndex = 0;
  std::optional<std::string> nextCursor;
};

class Server
{
public:
  using CompletionHandler = std::function<CompletionResult(const CompletionRequest &)>;

  static auto create(SessionOptions options = {}) -> std::shared_ptr<Server>;
  static auto create(ServerConfiguration configuration) -> std::shared_ptr<Server>;

  explicit Server(std::shared_ptr<Session> session);
  Server(std::shared_ptr<Session> session, ServerConfiguration configuration);

  auto configuration() const noexcept -> const ServerConfiguration &;

  auto session() const noexcept -> const std::shared_ptr<Session> &;

  auto start() -> void;
  auto stop() -> void;

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  auto setOutboundMessageSender(jsonrpc::OutboundMessageSender sender) -> void;

  auto handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>;
  auto handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void;
  auto handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool;

  auto sendRequest(const jsonrpc::RequestContext &context, jsonrpc::Request request, jsonrpc::OutboundRequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendNotification(const jsonrpc::RequestContext &context, jsonrpc::Notification notification) -> void;

  auto setCompletionHandler(CompletionHandler handler) -> void;
  auto emitLogMessage(const jsonrpc::RequestContext &context, LogLevel level, jsonrpc::JsonValue data, std::optional<std::string> logger = std::nullopt) -> bool;
  auto logLevel() const -> LogLevel;

  static auto paginateList(ListEndpoint endpoint, const std::optional<std::string> &cursor, std::size_t totalItems, std::size_t pageSize) -> PaginationWindow;

private:
  auto configureSessionInitialization() -> void;
  auto registerCoreHandlers() -> void;
  auto handleLoggingSetLevelRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleCompletionCompleteRequest(const jsonrpc::Request &request) -> jsonrpc::Response;

  auto isMethodEnabledByCapability(std::string_view method) const -> bool;
  static auto isCoreRequestMethod(std::string_view method) -> bool;

  std::shared_ptr<Session> session_;
  ServerConfiguration configuration_;
  jsonrpc::Router router_;
  mutable std::mutex utilityMutex_;
  CompletionHandler completionHandler_;
  LogLevel logLevel_ = LogLevel::kDebug;
};

}  // namespace mcp
