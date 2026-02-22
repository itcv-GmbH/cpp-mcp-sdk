#pragma once

#include <optional>
#include <string_view>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/util/task.hpp>

namespace mcp::util
{

struct CreateTaskResult
{
  Task task;
  std::optional<jsonrpc::JsonValue> metadata;
};

auto createTaskResultToJson(const CreateTaskResult &result) -> jsonrpc::JsonValue;
auto injectRelatedTaskMetadata(jsonrpc::JsonValue &resultObject, std::string_view taskId) -> void;

}  // namespace mcp::util
