#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/jsonrpc/router.hpp>
#include <mcp/version.hpp>

namespace mcp
{

namespace transport
{
class Transport;
}

class Executor
{
public:
  virtual ~Executor() = default;
  virtual auto post(std::function<void()> task) -> void = 0;
};

enum class HandlerThreadingPolicy
{
  kIoThread,
  kExecutor,
};

struct SessionThreading
{
  HandlerThreadingPolicy handlerThreadingPolicy = HandlerThreadingPolicy::kExecutor;
  std::shared_ptr<Executor> handlerExecutor;
};

struct SessionOptions
{
  SessionThreading threading;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
  };
};

struct RequestOptions
{
  std::chrono::milliseconds timeout {0};
  bool cancelOnTimeout = true;
};

using ResponseCallback = std::function<void(const jsonrpc::Response &)>;

enum class SessionState
{
  kCreated,
  kInitializing,
  kOperating,
  kStopping,
  kStopped,
};

class Session : public std::enable_shared_from_this<Session>
{
public:
  explicit Session(SessionOptions options = {});

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  auto sendRequest(std::string method, jsonrpc::JsonValue params, RequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendRequestAsync(std::string method, jsonrpc::JsonValue params, ResponseCallback callback, RequestOptions options = {}) -> void;
  auto sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params = std::nullopt) -> void;

  auto attachTransport(std::shared_ptr<transport::Transport> transport) -> void;
  auto start() -> void;
  auto stop() -> void;

  auto state() const noexcept -> SessionState;
  auto negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>;
  auto supportedProtocolVersions() const -> const std::vector<std::string> &;

private:
  SessionOptions options_;
  jsonrpc::Router router_;
  std::shared_ptr<transport::Transport> transport_;
  NegotiatedProtocolVersion negotiatedVersion_;
  SessionState state_ = SessionState::kCreated;
  mutable std::mutex mutex_;
};

}  // namespace mcp
