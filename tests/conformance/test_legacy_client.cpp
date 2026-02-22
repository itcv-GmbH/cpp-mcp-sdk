#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/client/client.hpp>
#include <mcp/http/all.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/transport/http.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;
namespace mcp_transport = mcp::transport;

auto makeServerOptions() -> mcp_transport::HttpServerOptions
{
  mcp_transport::HttpServerOptions options;
  options.endpoint.path = "/mcp";
  options.endpoint.bindAddress = "127.0.0.1";
  options.endpoint.bindLocalhostOnly = true;
  options.endpoint.port = 0;
  return options;
}

auto normalizePath(std::string path, std::string_view fallback) -> std::string
{
  const std::string_view trimmed = mcp_http::detail::trimAsciiWhitespace(path);
  if (trimmed.empty())
  {
    return std::string(fallback);
  }

  if (trimmed.front() == '/')
  {
    return std::string(trimmed);
  }

  return "/" + std::string(trimmed);
}

class LegacyHttpSseFixture
{
public:
  explicit LegacyHttpSseFixture(std::uint16_t modernInitializeStatus, std::string endpointEventData = "/rpc")
    : runtime_(makeServerOptions())
    , modernInitializeStatus_(modernInitializeStatus)
    , endpointEventData_(std::move(endpointEventData))
    , legacyRpcPath_(normalizePath(endpointEventData_, "/rpc"))
  {
    runtime_.setRequestHandler([this](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return handleRequest(request); });
    runtime_.start();
  }

  ~LegacyHttpSseFixture() { runtime_.stop(); }

  [[nodiscard]] auto endpointUrl() const -> std::string { return "http://127.0.0.1:" + std::to_string(runtime_.localPort()) + "/mcp"; }

  [[nodiscard]] auto modernInitializePostCount() const -> std::size_t
  {
    const std::scoped_lock lock(mutex_);
    return modernInitializePostCount_;
  }

  [[nodiscard]] auto legacySseGetCount() const -> std::size_t
  {
    const std::scoped_lock lock(mutex_);
    return legacySseGetCount_;
  }

  [[nodiscard]] auto legacyRpcPostCount() const -> std::size_t
  {
    const std::scoped_lock lock(mutex_);
    return legacyRpcPostCount_;
  }

private:
  static auto parseJsonRpcRequest(const std::string &body) -> std::optional<mcp::jsonrpc::Request>
  {
    try
    {
      const mcp::jsonrpc::Message message = mcp::jsonrpc::parseMessage(body);
      if (!std::holds_alternative<mcp::jsonrpc::Request>(message))
      {
        return std::nullopt;
      }

      return std::get<mcp::jsonrpc::Request>(message);
    }
    catch (const std::exception &error)
    {
      static_cast<void>(error);
      return std::nullopt;
    }
  }

  static auto makeInitializeResponse(const mcp::jsonrpc::Request &request) -> mcp::jsonrpc::Message
  {
    mcp::jsonrpc::SuccessResponse response;
    response.id = request.id;
    response.result = mcp::jsonrpc::JsonValue::object();
    response.result["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
    response.result["capabilities"] = mcp::jsonrpc::JsonValue::object();
    response.result["capabilities"]["tools"] = mcp::jsonrpc::JsonValue::object();
    response.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
    response.result["serverInfo"]["name"] = "legacy-fixture";
    response.result["serverInfo"]["version"] = "1.0.0";
    return mcp::jsonrpc::Message {response};
  }

  static auto makeToolsListResponse(const mcp::jsonrpc::Request &request) -> mcp::jsonrpc::Message
  {
    mcp::jsonrpc::SuccessResponse response;
    response.id = request.id;
    response.result = mcp::jsonrpc::JsonValue::object();
    response.result["tools"] = mcp::jsonrpc::JsonValue::array();

    mcp::jsonrpc::JsonValue tool = mcp::jsonrpc::JsonValue::object();
    tool["name"] = "legacy-tool";
    tool["inputSchema"] = mcp::jsonrpc::JsonValue::object();
    tool["inputSchema"]["type"] = "object";
    response.result["tools"].push_back(std::move(tool));

    return mcp::jsonrpc::Message {response};
  }

  auto enqueueMessageEvent(mcp::jsonrpc::Message message) -> void
  {
    mcp::http::sse::Event event;
    event.event = "message";
    event.data = mcp::jsonrpc::serializeMessage(message);
    queuedEvents_.push_back(std::move(event));
  }

  auto makeSseResponse(std::vector<mcp::http::sse::Event> events) const -> mcp_http::ServerResponse
  {
    mcp_http::ServerResponse response;
    response.statusCode = 200;
    mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "text/event-stream");
    response.body = mcp::http::sse::encodeEvents(events);
    return response;
  }

  auto makeStatusResponse(std::uint16_t statusCode) const -> mcp_http::ServerResponse
  {
    mcp_http::ServerResponse response;
    response.statusCode = statusCode;
    return response;
  }

  auto handleModernInitializePost(const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
  {
    const auto maybeRequest = parseJsonRpcRequest(request.body);
    if (!maybeRequest.has_value() || maybeRequest->method != "initialize")
    {
      return makeStatusResponse(404);
    }

    const std::scoped_lock lock(mutex_);
    ++modernInitializePostCount_;
    return makeStatusResponse(modernInitializeStatus_);
  }

  auto handleLegacyEventsGet() -> mcp_http::ServerResponse
  {
    std::vector<mcp::http::sse::Event> events;
    {
      const std::scoped_lock lock(mutex_);
      ++legacySseGetCount_;

      if (!endpointEventSent_)
      {
        mcp::http::sse::Event endpointEvent;
        endpointEvent.event = "endpoint";
        endpointEvent.data = endpointEventData_;
        events.push_back(std::move(endpointEvent));
        endpointEventSent_ = true;
      }

      events.insert(events.end(), queuedEvents_.begin(), queuedEvents_.end());
      queuedEvents_.clear();
    }

    return makeSseResponse(std::move(events));
  }

  auto handleLegacyRpcPost(const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
  {
    const std::optional<mcp::jsonrpc::Request> maybeRequest = parseJsonRpcRequest(request.body);
    {
      const std::scoped_lock lock(mutex_);
      ++legacyRpcPostCount_;

      if (maybeRequest.has_value())
      {
        if (maybeRequest->method == "initialize")
        {
          enqueueMessageEvent(makeInitializeResponse(*maybeRequest));
        }
        else if (maybeRequest->method == "tools/list")
        {
          enqueueMessageEvent(makeToolsListResponse(*maybeRequest));
        }
      }
    }

    return makeStatusResponse(202);
  }

  auto handleRequest(const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
  {
    if (request.method == mcp_http::ServerRequestMethod::kPost && request.path == "/mcp")
    {
      return handleModernInitializePost(request);
    }

    if (request.method == mcp_http::ServerRequestMethod::kGet && request.path == "/events")
    {
      return handleLegacyEventsGet();
    }

    if (request.method == mcp_http::ServerRequestMethod::kPost && request.path == legacyRpcPath_)
    {
      return handleLegacyRpcPost(request);
    }

    return makeStatusResponse(404);
  }

  mcp_transport::HttpServerRuntime runtime_;
  mutable std::mutex mutex_;
  std::uint16_t modernInitializeStatus_;
  std::string endpointEventData_;
  std::string legacyRpcPath_;
  bool endpointEventSent_ = false;
  std::size_t modernInitializePostCount_ = 0;
  std::size_t legacySseGetCount_ = 0;
  std::size_t legacyRpcPostCount_ = 0;
  std::vector<mcp::http::sse::Event> queuedEvents_;
};

}  // namespace

TEST_CASE("Legacy client fallback connects and lists tools when enabled", "[conformance][legacy_client]")
{
  for (const std::uint16_t fallbackStatus : std::vector<std::uint16_t> {400, 404, 405})
  {
    INFO("fallback status: " << fallbackStatus);

    LegacyHttpSseFixture fixture(fallbackStatus);

    auto client = mcp::Client::create();
    mcp_transport::HttpClientOptions clientOptions;
    clientOptions.endpointUrl = fixture.endpointUrl();
    clientOptions.enableLegacyHttpSseFallback = true;

    client->connectHttp(clientOptions);
    client->start();

    const auto initializeResponse = client->initialize().get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

    const mcp::ListToolsResult tools = client->listTools();
    REQUIRE(tools.tools.size() == 1);
    REQUIRE(tools.tools.front().name == "legacy-tool");

    REQUIRE(fixture.modernInitializePostCount() == 1);
    REQUIRE(fixture.legacySseGetCount() >= 1);
    REQUIRE(fixture.legacyRpcPostCount() >= 2);

    client->stop();
  }
}

TEST_CASE("Legacy client fallback disabled keeps initialize failure", "[conformance][legacy_client]")
{
  LegacyHttpSseFixture fixture(404);

  auto client = mcp::Client::create();
  mcp_transport::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = fixture.endpointUrl();
  clientOptions.enableLegacyHttpSseFallback = false;

  client->connectHttp(clientOptions);
  client->start();

  const auto initializeResponse = client->initialize().get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(initializeResponse));
  REQUIRE(fixture.modernInitializePostCount() == 1);
  REQUIRE(fixture.legacySseGetCount() == 0);
  REQUIRE(fixture.legacyRpcPostCount() == 0);

  client->stop();
}

TEST_CASE("Legacy client fallback does not trigger on HTTP 401", "[conformance][legacy_client]")
{
  LegacyHttpSseFixture fixture(401);

  auto client = mcp::Client::create();
  mcp_transport::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = fixture.endpointUrl();
  clientOptions.enableLegacyHttpSseFallback = true;

  client->connectHttp(clientOptions);
  client->start();

  const auto initializeResponse = client->initialize().get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(initializeResponse));
  REQUIRE(fixture.modernInitializePostCount() == 1);
  REQUIRE(fixture.legacySseGetCount() == 0);
  REQUIRE(fixture.legacyRpcPostCount() == 0);

  client->stop();
}
