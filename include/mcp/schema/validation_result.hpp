#pragma once

#include <string>
#include <vector>

#include <mcp/schema/validation_diagnostic.hpp>

namespace mcp::schema
{

struct ValidationResult
{
  bool valid = false;
  std::string effectiveDialect;
  std::vector<ValidationDiagnostic> diagnostics;
};

}  // namespace mcp::schema
