#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mcp/jsonrpc/request.hpp>
#include <mcp/jsonrpc/response.hpp>
#include <mcp/jsonrpc/success_response.hpp>
#include <mcp/jsonrpc/types.hpp>
#include <mcp/server/list_endpoint.hpp>
#include <mcp/server/pagination_window.hpp>

namespace mcp::server::detail
{

auto parseCursorFromParams(const jsonrpc::Request &request, ListEndpoint endpoint, std::optional<jsonrpc::Response> *errorResponse = nullptr) -> std::optional<std::string>;

auto applyPagination(ListEndpoint endpoint,
                     const std::optional<std::string> &cursor,
                     std::size_t totalItems,
                     std::size_t pageSize,
                     std::optional<jsonrpc::Response> *errorResponse = nullptr) -> PaginationWindow;

template<typename T, typename SerializeFn>
auto buildListResponse(const jsonrpc::RequestId &requestId,
                       const std::vector<T> &items,
                       const PaginationWindow &window,
                       SerializeFn &&serializeFn,  // NOLINT(cppcoreguidelines-missing-std-forward) - called multiple times, cannot forward
                       std::string_view resultKey) -> jsonrpc::SuccessResponse
{
  jsonrpc::JsonValue itemsJson = jsonrpc::JsonValue::array();
  for (std::size_t index = window.startIndex; index < window.endIndex; ++index)
  {
    itemsJson.push_back(serializeFn(items[index]));
  }

  jsonrpc::SuccessResponse response;
  response.id = requestId;
  response.result = jsonrpc::JsonValue::object();
  response.result[resultKey] = std::move(itemsJson);
  if (window.nextCursor.has_value())
  {
    response.result["nextCursor"] = *window.nextCursor;
  }

  return response;
}

}  // namespace mcp::server::detail
