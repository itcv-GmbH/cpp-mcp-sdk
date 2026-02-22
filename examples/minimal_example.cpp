// Minimal example for MCP SDK
#include <iostream>

#include <mcp/sdk/version.hpp>

auto main() -> int
{
  std::cout << "MCP SDK Version: " << mcp::getLibraryVersion() << '\n';
  return 0;
}
