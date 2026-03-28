#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <mcp/export.hpp>

namespace mcp::security
{

MCP_SDK_EXPORT auto cryptoRandomBytes(std::size_t length) -> std::vector<std::uint8_t>;

}  // namespace mcp::security
