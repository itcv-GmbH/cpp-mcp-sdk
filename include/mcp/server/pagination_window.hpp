#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace mcp
{

struct PaginationWindow
{
  std::size_t startIndex = 0;
  std::size_t endIndex = 0;
  std::optional<std::string> nextCursor;
};

}  // namespace mcp
