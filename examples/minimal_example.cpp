// Minimal example for MCP SDK
#include <iostream>

#include <mcp/sdk/version.hpp>

auto main() -> int
{
  std::cout << "MCP SDK Version: " << mcp::sdk::get_version() << '\n';
  return 0;
}
