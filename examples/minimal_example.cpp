// Minimal example for MCP SDK
#include <iostream>

#include <mcp/version.hpp>

auto main() -> int
{
  std::cout << "MCP SDK Version: " << mcp::getLibraryVersion() << '\n';
  return 0;
}
