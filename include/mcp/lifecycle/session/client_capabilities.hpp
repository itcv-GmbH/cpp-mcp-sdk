#pragma once

#include <optional>
#include <string_view>

#include <jsoncons/json.hpp>
#include <mcp/export.hpp>
#include <mcp/lifecycle/session/elicitation_capability.hpp>
#include <mcp/lifecycle/session/roots_capability.hpp>
#include <mcp/lifecycle/session/sampling_capability.hpp>
#include <mcp/lifecycle/session/tasks_capability.hpp>

namespace mcp::lifecycle::session
{

/**
 * @brief Capabilities a client may support.
 */
class MCP_SDK_EXPORT ClientCapabilities
{
public:
  ClientCapabilities() = default;
  ClientCapabilities(std::optional<RootsCapability> roots,
                     std::optional<SamplingCapability> sampling,
                     std::optional<ElicitationCapability> elicitation,
                     std::optional<TasksCapability> tasks,
                     std::optional<jsoncons::json> experimental);

  auto roots() const noexcept -> const std::optional<RootsCapability> &;
  auto sampling() const noexcept -> const std::optional<SamplingCapability> &;
  auto elicitation() const noexcept -> const std::optional<ElicitationCapability> &;
  auto tasks() const noexcept -> const std::optional<TasksCapability> &;
  auto experimental() const noexcept -> const std::optional<jsoncons::json> &;

  auto hasCapability(std::string_view capability) const -> bool;

private:
  std::optional<RootsCapability> roots_;
  std::optional<SamplingCapability> sampling_;
  std::optional<ElicitationCapability> elicitation_;
  std::optional<TasksCapability> tasks_;
  std::optional<jsoncons::json> experimental_;  // Passthrough for experimental features.
};

}  // namespace mcp::lifecycle::session
