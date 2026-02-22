#pragma once

#include <string>

namespace mcp::schema
{

struct ValidationDiagnostic
{
  std::string instanceLocation;
  std::string evaluationPath;
  std::string schemaLocation;
  std::string message;
};

}  // namespace mcp::schema
