#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>

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

struct CompletionResult
{
  std::vector<std::string> values;
  std::optional<std::size_t> total;
  std::optional<bool> hasMore;
};

}  // namespace mcp
