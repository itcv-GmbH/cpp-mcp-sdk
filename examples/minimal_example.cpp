// Minimal example for MCP SDK
#include <iostream>

#include <mcp/sdk/version.hpp>

int main()
{
  std::cout << "MCP SDK Version: " << mcp::sdk::get_version() << std::endl;
  return 0;
}
