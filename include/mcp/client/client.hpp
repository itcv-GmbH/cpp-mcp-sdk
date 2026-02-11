#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <mcp/client/elicitation.hpp>
#include <mcp/client/roots.hpp>
#include <mcp/client/sampling.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/stdio.hpp>
#include <mcp/transport/transport.hpp>

namespace mcp
{

inline constexpr std::size_t kDefaultMaxPaginationPages = 1024U;

struct ClientInitializeConfiguration
{
  std::optional<std::string> protocolVersion;
  std::optional<ClientCapabilities> capabilities;
  std::optional<Implementation> clientInfo;
};

struct ListToolsResult
{
  std::vector<ToolDefinition> tools;
  std::optional<std::string> nextCursor;
};

struct ListResourcesResult
{
  std::vector<ResourceDefinition> resources;
  std::optional<std::string> nextCursor;
};

struct ReadResourceResult
{
  std::vector<ResourceContent> contents;
};

struct ListResourceTemplatesResult
{
  std::vector<ResourceTemplateDefinition> resourceTemplates;
  std::optional<std::string> nextCursor;
};

struct ListPromptsResult
{
  std::vector<PromptDefinition> prompts;
  std::optional<std::string> nextCursor;
};

class Client
{
public:
  static auto create(SessionOptions options = {}) -> std::shared_ptr<Client>;

  explicit Client(std::shared_ptr<Session> session);

  auto session() const noexcept -> const std::shared_ptr<Session> &;

  auto attachTransport(std::shared_ptr<transport::Transport> transport) -> void;
  auto connectStdio(const transport::StdioClientOptions &options) -> void;
  auto connectHttp(const transport::HttpClientOptions &options) -> void;
  auto connectHttp(transport::http::StreamableHttpClientOptions options, transport::http::StreamableHttpClient::RequestExecutor requestExecutor) -> void;

  auto setInitializeConfiguration(ClientInitializeConfiguration configuration) -> void;
  auto initializeConfiguration() const -> ClientInitializeConfiguration;
  auto initialize(RequestOptions options = {}) -> std::future<jsonrpc::Response>;

  auto listTools(std::optional<std::string> cursor = std::nullopt, RequestOptions options = {}) -> ListToolsResult;
  auto callTool(const std::string &name, jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object(), RequestOptions options = {}) -> CallToolResult;
  auto listResources(std::optional<std::string> cursor = std::nullopt, RequestOptions options = {}) -> ListResourcesResult;
  auto readResource(const std::string &uri, RequestOptions options = {}) -> ReadResourceResult;
  auto listResourceTemplates(std::optional<std::string> cursor = std::nullopt, RequestOptions options = {}) -> ListResourceTemplatesResult;
  auto listPrompts(std::optional<std::string> cursor = std::nullopt, RequestOptions options = {}) -> ListPromptsResult;
  auto getPrompt(const std::string &name, jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object(), RequestOptions options = {}) -> PromptGetResult;
  auto setRootsProvider(RootsProvider provider) -> void;
  auto clearRootsProvider() -> void;
  auto notifyRootsListChanged() -> bool;
  auto setSamplingCreateMessageHandler(SamplingCreateMessageHandler handler) -> void;
  auto clearSamplingCreateMessageHandler() -> void;
  auto setFormElicitationHandler(FormElicitationHandler handler) -> void;
  auto clearFormElicitationHandler() -> void;
  auto setUrlElicitationHandler(UrlElicitationHandler handler) -> void;
  auto clearUrlElicitationHandler() -> void;
  auto setUrlElicitationCompletionHandler(UrlElicitationCompletionHandler handler) -> void;
  auto clearUrlElicitationCompletionHandler() -> void;

  template<typename FetchPage, typename ConsumePage>
  auto forEachPage(FetchPage fetchPage, ConsumePage consumePage, std::optional<std::string> cursor = std::nullopt, std::size_t maxPages = kDefaultMaxPaginationPages) -> void
  {
    std::unordered_set<std::string> seenCursors;
    std::size_t fetchedPages = 0;

    while (true)
    {
      if (cursor.has_value())
      {
        const auto insertResult = seenCursors.insert(*cursor);
        if (!insertResult.second)
        {
          throw std::runtime_error("Pagination cursor cycle detected");
        }
      }

      if (fetchedPages >= maxPages)
      {
        throw std::runtime_error("Pagination exceeded maximum page limit");
      }

      const auto page = fetchPage(cursor);
      ++fetchedPages;
      consumePage(page);

      if (!page.nextCursor.has_value())
      {
        break;
      }

      cursor = page.nextCursor;
    }
  }

  template<typename ItemType, typename FetchPage, typename ExtractItems>
  auto collectAllPages(FetchPage fetchPage, ExtractItems extractItems, std::optional<std::string> cursor = std::nullopt, std::size_t maxPages = kDefaultMaxPaginationPages)
    -> std::vector<ItemType>
  {
    std::vector<ItemType> allItems;

    forEachPage(
      std::move(fetchPage),
      [&allItems, &extractItems](const auto &page) -> void
      {
        const auto &pageItems = extractItems(page);
        allItems.insert(allItems.end(), pageItems.begin(), pageItems.end());
      },
      std::move(cursor),
      maxPages);

    return allItems;
  }

  auto start() -> void;
  auto stop() -> void;

  auto sendRequest(std::string method, jsonrpc::JsonValue params, RequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendRequestAsync(std::string method, jsonrpc::JsonValue params, const ResponseCallback &callback, RequestOptions options = {}) -> void;
  auto sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params = std::nullopt) -> void;

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  auto handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>;
  auto handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void;
  auto handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool;
  auto handleMessage(const jsonrpc::RequestContext &context, const jsonrpc::Message &message) -> void;

  auto negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>;
  auto negotiatedParameters() const -> std::optional<NegotiatedParameters>;
  auto negotiatedClientCapabilities() const -> std::optional<ClientCapabilities>;
  auto negotiatedServerCapabilities() const -> std::optional<ServerCapabilities>;

  auto supportedProtocolVersions() const -> std::vector<std::string>;

private:
  auto nextRequestId() -> jsonrpc::RequestId;
  auto applyInitializeDefaults(jsonrpc::Request &request) const -> void;
  auto dispatchOutboundMessage(jsonrpc::Message message) -> void;
  auto isPendingInitializeResponse(const jsonrpc::Response &response) const -> bool;

  mutable std::mutex mutex_;
  std::shared_ptr<Session> session_;
  jsonrpc::Router router_;
  std::shared_ptr<transport::Transport> transport_;
  ClientInitializeConfiguration initializeConfiguration_;
  std::optional<RootsProvider> rootsProvider_;
  std::optional<SamplingCreateMessageHandler> samplingCreateMessageHandler_;
  std::optional<FormElicitationHandler> formElicitationHandler_;
  std::optional<UrlElicitationHandler> urlElicitationHandler_;
  std::optional<UrlElicitationCompletionHandler> urlElicitationCompletionHandler_;
  std::unordered_set<std::string> pendingUrlElicitationIds_;
  std::optional<jsonrpc::RequestId> pendingInitializeRequestId_;
  std::int64_t nextRequestId_ = 1;
};

}  // namespace mcp
