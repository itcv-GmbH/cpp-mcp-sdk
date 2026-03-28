#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/export.hpp>

namespace mcp::util
{

inline constexpr std::string_view kRelatedTaskMetadataKey = "io.modelcontextprotocol/related-task";

enum class TaskStatus : std::uint8_t
{
  kWorking,
  kInputRequired,
  kCompleted,
  kFailed,
  kCancelled,
};

MCP_SDK_EXPORT auto toString(TaskStatus status) -> std::string_view;
MCP_SDK_EXPORT auto taskStatusFromString(std::string_view status) -> std::optional<TaskStatus>;
MCP_SDK_EXPORT auto isTerminalTaskStatus(TaskStatus status) -> bool;
MCP_SDK_EXPORT auto isValidTaskStatusTransition(TaskStatus from, TaskStatus targetStatus) -> bool;

}  // namespace mcp::util
