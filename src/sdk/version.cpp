#include <mcp/sdk/version.hpp>


namespace mcp::sdk
{

auto getLibraryVersion() noexcept -> const char *
{
  return kSdkVersion.data();
}

} // namespace mcp::sdk

