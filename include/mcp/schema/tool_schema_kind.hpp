#pragma once

#include <cstdint>

namespace mcp::schema
{

enum class ToolSchemaKind : std::uint8_t
{
  kInput,
  kOutput,
};

}  // namespace mcp::schema
