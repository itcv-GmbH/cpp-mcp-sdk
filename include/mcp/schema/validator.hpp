#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <jsoncons/json.hpp>

namespace mcp::schema
{

using JsonValue = jsoncons::json;

enum class ToolSchemaKind : std::uint8_t
{
  kInput,
  kOutput,
};

struct ValidationDiagnostic
{
  std::string instanceLocation;
  std::string evaluationPath;
  std::string schemaLocation;
  std::string message;
};

struct ValidationResult
{
  bool valid = false;
  std::string effectiveDialect;
  std::vector<ValidationDiagnostic> diagnostics;
};

struct PinnedSchemaMetadata
{
  std::string localPath;
  std::string upstreamSchemaUrl;
  std::string upstreamRef;
  std::string sha256;
};

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

auto formatDiagnostics(const ValidationResult &result) -> std::string;

}  // namespace mcp::schema
