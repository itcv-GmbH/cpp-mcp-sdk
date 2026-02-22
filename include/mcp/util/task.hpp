#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/util/task_status.hpp>

namespace mcp::util
{

struct Task
{
  std::string taskId;
  TaskStatus status = TaskStatus::kWorking;
  std::optional<std::string> statusMessage;
  std::string createdAt;
  std::string lastUpdatedAt;
  std::optional<std::int64_t> ttl;
  std::optional<std::int64_t> pollInterval;
};

auto taskToJson(const Task &task) -> jsonrpc::JsonValue;

}  // namespace mcp::util
