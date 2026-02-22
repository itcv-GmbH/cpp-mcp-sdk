#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include <jsoncons/json.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Request ID type - can be either an integer or string.
 */
using RequestId = std::variant<std::int64_t, std::string>;

/**
 * @brief JSON value type alias using jsoncons.
 */
using JsonValue = jsoncons::json;

}  // namespace mcp::jsonrpc
