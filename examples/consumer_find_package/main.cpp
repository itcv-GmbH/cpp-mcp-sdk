#include <iostream>

#include <mcp/version.hpp>

#ifndef MCP_SDK_ENABLE_TLS
#  define MCP_SDK_ENABLE_TLS -1
#endif

#ifndef MCP_SDK_ENABLE_AUTH
#  define MCP_SDK_ENABLE_AUTH -1
#endif

auto main() -> int
{
  std::cout << "MCP SDK version: " << mcp::getLibraryVersion() << '\n';
  std::cout << "MCP_SDK_ENABLE_TLS=" << MCP_SDK_ENABLE_TLS << '\n';
  std::cout << "MCP_SDK_ENABLE_AUTH=" << MCP_SDK_ENABLE_AUTH << '\n';
  return 0;
}
