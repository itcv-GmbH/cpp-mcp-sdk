#include "mcp/server/detail/list_handler_helpers.hpp"

#include "mcp/server/detail/helpers.hpp"

namespace mcp::server::detail
{

auto parseCursorFromParams(const jsonrpc::Request &request, ListEndpoint endpoint, std::optional<jsonrpc::Response> *errorResponse) -> std::optional<std::string>
{
  std::optional<std::string> cursor;

  if (request.params.has_value())
  {
    if (!request.params->is_object())
    {
      if (errorResponse != nullptr)
      {
        *errorResponse = detail::makeInvalidParamsResponse(request.id, std::string(endpointName(endpoint)) + "/list requires params to be an object when provided");
      }
      return std::nullopt;
    }

    if (request.params->contains("cursor"))
    {
      if (!(*request.params)["cursor"].is_string())
      {
        if (errorResponse != nullptr)
        {
          *errorResponse = detail::makeInvalidParamsResponse(request.id, std::string(endpointName(endpoint)) + "/list requires params.cursor to be a string");
        }
        return std::nullopt;
      }

      cursor = (*request.params)["cursor"].as<std::string>();
    }
  }

  return cursor;
}

auto applyPagination(ListEndpoint endpoint, const std::optional<std::string> &cursor, std::size_t totalItems, std::size_t pageSize, std::optional<jsonrpc::Response> *errorResponse)
  -> PaginationWindow
{
  PaginationWindow window;
  try
  {
    if (pageSize == 0)
    {
      throw std::invalid_argument("Pagination page size must be greater than zero");
    }

    std::size_t startIndex = 0;
    if (cursor.has_value())
    {
      const auto parsedCursor = detail::parsePaginationCursor(endpoint, *cursor);
      if (!parsedCursor.has_value() || *parsedCursor > totalItems)
      {
        throw std::invalid_argument("Invalid pagination cursor");
      }

      startIndex = *parsedCursor;
    }

    const std::size_t endIndex = std::min(startIndex + pageSize, totalItems);

    window.startIndex = startIndex;
    window.endIndex = endIndex;
    if (endIndex < totalItems)
    {
      window.nextCursor = detail::makePaginationCursor(endpoint, endIndex);
    }
  }
  catch (const std::invalid_argument &)
  {
    if (errorResponse != nullptr)
    {
      *errorResponse = detail::makeInvalidParamsResponse(jsonrpc::RequestId {}, "Invalid " + std::string(endpointName(endpoint)) + "/list cursor");
    }
    return PaginationWindow {0, 0, std::nullopt};
  }
  return window;
}

}  // namespace mcp::server::detail
