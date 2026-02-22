#pragma once

#include <string>
#include <string_view>

#include <jsoncons/json.hpp>
#include <mcp/schema/pinned_schema_metadata.hpp>
#include <mcp/schema/tool_schema_kind.hpp>
#include <mcp/schema/validation_result.hpp>

namespace mcp::schema
{

using JsonValue = jsoncons::json;

class Validator
{
public:
  static auto loadPinnedMcpSchema() -> Validator;

  auto validateInstance(const JsonValue &instance, std::string_view definitionName = "JSONRPCMessage") const -> ValidationResult;
  auto validateMcpMethodMessage(const JsonValue &messageJson) const -> ValidationResult;
  auto validateToolSchema(const JsonValue &schemaObject, ToolSchemaKind kind = ToolSchemaKind::kInput) const -> ValidationResult;

  auto metadata() const noexcept -> const PinnedSchemaMetadata &;

private:
  JsonValue schemaDocument_;
  PinnedSchemaMetadata metadata_;
};

}  // namespace mcp::schema
