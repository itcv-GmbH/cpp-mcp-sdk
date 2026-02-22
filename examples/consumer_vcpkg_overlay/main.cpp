#include <iostream>

#include <mcp/sdk/version.hpp>

auto main() -> int
{
  std::cout << "MCP SDK version: " << mcp::getLibraryVersion() << '\n';
  return 0;
}
