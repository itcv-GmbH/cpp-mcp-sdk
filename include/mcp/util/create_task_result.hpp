#pragma once

#include <optional>
#include <string_view>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/util/task.hpp>

namespace mcp::util
{

struct CreateTaskResult
{
  Task task;
  std::optional<jsonrpc::JsonValue> metadata;
};

MCP_SDK_EXPORT auto createTaskResultToJson(const CreateTaskResult &result) -> jsonrpc::JsonValue;
MCP_SDK_EXPORT auto injectRelatedTaskMetadata(jsonrpc::JsonValue &resultObject, std::string_view taskId) -> void;

}  // namespace mcp::util
