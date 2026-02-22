#pragma once

#include <string_view>

namespace mcp::schema::detail
{

auto pinnedSchemaJson() -> std::string_view;
auto pinnedSchemaSourcePath() -> std::string_view;

}  // namespace mcp::schema::detail
