// Smoke test for MCP SDK
#include <cstdlib>

#include <mcp/sdk/version.hpp>

auto main() -> int
{
  const char *version = mcp::sdk::get_version();
  return (version != nullptr) ? EXIT_SUCCESS : EXIT_FAILURE;
}
