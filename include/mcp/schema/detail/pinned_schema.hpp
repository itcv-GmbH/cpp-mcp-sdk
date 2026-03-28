#pragma once

#include <mcp/export.hpp>
#include <string_view>

namespace mcp::schema::detail
{

MCP_SDK_EXPORT auto pinnedSchemaJson() -> std::string_view;
MCP_SDK_EXPORT auto pinnedSchemaSourcePath() -> std::string_view;

}  // namespace mcp::schema::detail
