#pragma once

#include <cstdint>

namespace mcp::transport::http
{

enum class RequestKind : std::uint8_t
{
  kInitialize,
  kOther,
};

}  // namespace mcp::transport::http