#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mcp::security
{

auto cryptoRandomBytes(std::size_t length) -> std::vector<std::uint8_t>;

}  // namespace mcp::security
