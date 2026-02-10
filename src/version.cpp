#include <mcp/version.hpp>

namespace mcp
{

auto getLibraryVersion() noexcept -> const char *
{
  return kSdkVersion.data();
}

}  // namespace mcp
