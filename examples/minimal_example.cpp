// Minimal example for MCP SDK
#include <mcp/sdk/version.hpp>
#include <iostream>

int main() {
    std::cout << "MCP SDK Version: " << mcp::sdk::get_version() << std::endl;
    return 0;
}
