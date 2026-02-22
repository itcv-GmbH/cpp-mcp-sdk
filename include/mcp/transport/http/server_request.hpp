#pragma once

#include <cstdint>
#include <string>

#include <mcp/transport/http/header.hpp>

namespace mcp::transport::http
{

enum class ServerRequestMethod : std::uint8_t
{
  kGet,
  kPost,
  kDelete,
};

struct ServerRequest
{
  ServerRequestMethod method = ServerRequestMethod::kPost;
  std::string path = "/mcp";
  HeaderList headers;
  std::string body;
};

}  // namespace mcp::transport::http