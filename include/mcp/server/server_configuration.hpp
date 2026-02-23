#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <mcp/lifecycle/session.hpp>
#include <mcp/session.hpp>
#include <mcp/util/all.hpp>

namespace mcp::server
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

}  // namespace mcp::server
