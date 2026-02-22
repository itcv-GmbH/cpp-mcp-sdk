#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp
{

enum class CompletionReferenceType : std::uint8_t
{
  kPrompt,
  kResource,
};

struct CompletionRequest
{
  CompletionReferenceType referenceType = CompletionReferenceType::kPrompt;
  std::string referenceValue;
  std::string argumentName;
  std::string argumentValue;
  std::optional<jsonrpc::JsonValue> contextArguments;
};

}  // namespace mcp
