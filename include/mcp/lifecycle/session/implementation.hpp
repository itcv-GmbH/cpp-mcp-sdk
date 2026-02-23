#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/lifecycle/session/icon.hpp>

namespace mcp
{
namespace lifecycle
{
namespace session
{

/**
 * @brief Describes MCP implementation metadata.
 */
class Implementation
{
public:
  Implementation() = default;
  Implementation(std::string name,
                 std::string version,
                 std::optional<std::string> title = std::nullopt,
                 std::optional<std::string> description = std::nullopt,
                 std::optional<std::string> websiteUrl = std::nullopt,
                 std::optional<std::vector<Icon>> icons = std::nullopt);

  auto name() const noexcept -> const std::string & { return name_; }
  auto version() const noexcept -> const std::string & { return version_; }
  auto title() const noexcept -> const std::optional<std::string> & { return title_; }
  auto description() const noexcept -> const std::optional<std::string> & { return description_; }
  auto websiteUrl() const noexcept -> const std::optional<std::string> & { return websiteUrl_; }
  auto icons() const noexcept -> const std::optional<std::vector<Icon>> & { return icons_; }

private:
  std::string name_;
  std::string version_;
  std::optional<std::string> title_;
  std::optional<std::string> description_;
  std::optional<std::string> websiteUrl_;
  std::optional<std::vector<Icon>> icons_;
};

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
