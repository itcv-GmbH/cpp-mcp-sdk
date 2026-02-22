#pragma once

#include <mcp/sdk/version.hpp>

namespace mcp
{
namespace sdk
{

inline auto get_version() noexcept -> const char *
{
  return mcp::getLibraryVersion();
}

}  // namespace sdk
}  // namespace mcp
