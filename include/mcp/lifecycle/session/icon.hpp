#pragma once

#include <optional>
#include <string>
#include <vector>



namespace mcp::lifecycle::session
{

/**
 * @brief Represents an optionally-sized icon for UI display.
 */
class Icon
{
public:
  Icon() = default;
  explicit Icon(std::string src,
                std::optional<std::string> mimeType = std::nullopt,
                std::optional<std::vector<std::string>> sizes = std::nullopt,
                std::optional<std::string> theme = std::nullopt);

  auto src() const noexcept -> const std::string & { return src_; }
  auto mimeType() const noexcept -> const std::optional<std::string> & { return mimeType_; }
  auto sizes() const noexcept -> const std::optional<std::vector<std::string>> & { return sizes_; }
  auto theme() const noexcept -> const std::optional<std::string> & { return theme_; }

private:
  std::string src_;
  std::optional<std::string> mimeType_;
  std::optional<std::vector<std::string>> sizes_;
  std::optional<std::string> theme_;
};

} // namespace mcp::lifecycle::session


