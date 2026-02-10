// Smoke test for MCP SDK
#include <mcp/sdk/version.hpp>
#include <cstdlib>

int main() {
    const char* version = mcp::sdk::get_version();
    return version ? EXIT_SUCCESS : EXIT_FAILURE;
}
