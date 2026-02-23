#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/transport/http/header.hpp>
#include <mcp/transport/http/header_utils.hpp>
#include <mcp/transport/http/sse_stream_response.hpp>

namespace mcp::transport::http
{

struct ServerResponse
{
  std::uint16_t statusCode = detail::kHttpStatusOk;
  HeaderList headers;
  std::string body;
  std::optional<SseStreamResponse> sse;
};

}  // namespace mcp::transport::http
