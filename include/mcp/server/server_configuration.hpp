#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/lifecycle/session.hpp>
#include <mcp/sdk/error_reporter.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/session.hpp>
#include <mcp/util/all.hpp>

namespace mcp
{

struct ServerConfiguration
{
  lifecycle::session::SessionOptions sessionOptions;
  ServerCapabilities capabilities;
  std::optional<Implementation> serverInfo;
  std::optional<std::string> instructions;
  std::shared_ptr<util::TaskStore> taskStore;
  std::optional<std::int64_t> defaultTaskPollInterval = util::kDefaultTaskPollIntervalMs;
  bool emitTaskStatusNotifications = false;
};

}  // namespace mcp
