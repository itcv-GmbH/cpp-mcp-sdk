#include <mcp/sdk/version.hpp>

namespace mcp
{
namespace sdk
{

auto getLibraryVersion() noexcept -> const char *
{
  return kSdkVersion.data();
}

}  // namespace sdk
}  // namespace mcp
