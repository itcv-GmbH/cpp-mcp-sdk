#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/all.hpp>

namespace mcp::util
{

struct TaskAugmentationRequest
{
  bool requested = false;
  bool ttlProvided = false;
  std::optional<std::int64_t> ttl;
};

MCP_SDK_EXPORT auto parseTaskAugmentation(const std::optional<jsonrpc::JsonValue> &params, std::string *errorMessage = nullptr) -> TaskAugmentationRequest;

}  // namespace mcp::util
