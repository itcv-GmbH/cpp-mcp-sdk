#pragma once

#include <optional>
#include <string_view>

#include <jsoncons/json.hpp>
#include <mcp/export.hpp>
#include <mcp/lifecycle/session/completions_capability.hpp>
#include <mcp/lifecycle/session/logging_capability.hpp>
#include <mcp/lifecycle/session/prompts_capability.hpp>
#include <mcp/lifecycle/session/resources_capability.hpp>
#include <mcp/lifecycle/session/tasks_capability.hpp>
#include <mcp/lifecycle/session/tools_capability.hpp>

namespace mcp::lifecycle::session
{

/**
 * @brief Capabilities a server may support.
 */
class MCP_SDK_EXPORT ServerCapabilities
{
public:
  ServerCapabilities() = default;
  ServerCapabilities(std::optional<LoggingCapability> logging,
                     std::optional<CompletionsCapability> completions,
                     std::optional<PromptsCapability> prompts,
                     std::optional<ResourcesCapability> resources,
                     std::optional<ToolsCapability> tools,
                     std::optional<TasksCapability> tasks,
                     std::optional<jsoncons::json> experimental);

  auto logging() const noexcept -> const std::optional<LoggingCapability> &;
  auto completions() const noexcept -> const std::optional<CompletionsCapability> &;
  auto prompts() const noexcept -> const std::optional<PromptsCapability> &;
  auto resources() const noexcept -> const std::optional<ResourcesCapability> &;
  auto tools() const noexcept -> const std::optional<ToolsCapability> &;
  auto tasks() const noexcept -> const std::optional<TasksCapability> &;
  auto experimental() const noexcept -> const std::optional<jsoncons::json> &;

  auto hasCapability(std::string_view capability) const -> bool;

private:
  std::optional<LoggingCapability> logging_;
  std::optional<CompletionsCapability> completions_;
  std::optional<PromptsCapability> prompts_;
  std::optional<ResourcesCapability> resources_;
  std::optional<ToolsCapability> tools_;
  std::optional<TasksCapability> tasks_;
  std::optional<jsoncons::json> experimental_;  // Passthrough for experimental features.
};

}  // namespace mcp::lifecycle::session
