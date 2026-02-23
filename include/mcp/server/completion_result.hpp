#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mcp::server
{

struct CompletionResult
{
  std::vector<std::string> values;
  std::optional<std::size_t> total;
  std::optional<bool> hasMore;
};

}  // namespace mcp::server
