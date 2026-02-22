#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/lifecycle/session.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/util/tasks.hpp>

namespace mcp
{

struct ServerConfiguration
{
  SessionOptions sessionOptions;
  ServerCapabilities capabilities;
  std::optional<Implementation> serverInfo;
  std::optional<std::string> instructions;
  std::shared_ptr<util::TaskStore> taskStore;
  std::optional<std::int64_t> defaultTaskPollInterval = util::kDefaultTaskPollIntervalMs;
  bool emitTaskStatusNotifications = false;
};

}  // namespace mcp
