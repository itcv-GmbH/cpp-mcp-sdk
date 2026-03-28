#pragma once

#include <string>

#include <mcp/export.hpp>
#include <mcp/schema/validation_result.hpp>

namespace mcp::schema
{

MCP_SDK_EXPORT auto formatDiagnostics(const ValidationResult &result) -> std::string;

}  // namespace mcp::schema
