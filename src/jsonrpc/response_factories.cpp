#include "mcp/jsonrpc/response_factories.hpp"

namespace mcp::jsonrpc
{

auto makeSuccessResponse(const RequestId &requestId, JsonValue result) -> SuccessResponse
{
  SuccessResponse response;
  response.id = requestId;
  response.result = std::move(result);
  return response;
}

}  // namespace mcp::jsonrpc
