#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

auto toString(TaskStatus status) -> std::string_view;
auto taskStatusFromString(std::string_view status) -> std::optional<TaskStatus>;
auto isTerminalTaskStatus(TaskStatus status) -> bool;
auto isValidTaskStatusTransition(TaskStatus from, TaskStatus targetStatus) -> bool;

}  // namespace mcp::util
