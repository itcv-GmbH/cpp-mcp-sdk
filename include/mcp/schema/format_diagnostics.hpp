#pragma once

#include <string>

#include <mcp/schema/validation_result.hpp>

namespace mcp::schema
{

auto formatDiagnostics(const ValidationResult &result) -> std::string;

}  // namespace mcp::schema
