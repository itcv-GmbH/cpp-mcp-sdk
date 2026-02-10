// Smoke test for MCP SDK
#include <cstdlib>

#include <mcp/sdk/version.hpp>

int main()
{
  const char *version = mcp::sdk::get_version();
  return version ? EXIT_SUCCESS : EXIT_FAILURE;
}
