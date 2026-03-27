# MCP C++ SDK

[![Build Status](https://img.shields.io/github/actions/workflow/status/itcv-GmbH/cpp-mcp-sdk/ci.yml?branch=main&label=CI%20%7C%20Linux%20%E2%80%A2%20macOS%20%E2%80%A2%20Windows&logo=github)](https://github.com/itcv-GmbH/cpp-mcp-sdk/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg?logo=cplusplus)](https://isocpp.org/)
[![MCP Spec](https://img.shields.io/badge/MCP-2025--11--25-green.svg)](https://modelcontextprotocol.io/)

**The first fully MCP 2025-11-25 compliant C++ SDK** - Build production-ready MCP servers and clients with complete protocol support including Streamable HTTP transport and OAuth-based authorization.

## Features

### Complete MCP 2025-11-25 Implementation

- **Transports**: stdio + Streamable HTTP (single endpoint, resumable SSE)
- **Server Features**: Tools, Resources, Prompts with full CRUD operations
- **Client Features**: Roots, Sampling, Elicitation (form + URL modes)
- **Utilities**: Ping, Cancellation, Progress, Tasks, Pagination, Logging, Completion
- **Authorization**: OAuth 2.1 + PKCE, RFC9728 discovery, WWW-Authenticate challenges
- **Security**: Origin validation, SSRF protection, runtime limits, token storage abstraction

### Cross-Platform Support

- **Linux** (Ubuntu 24.04+)
- **macOS** (14+)
- **Windows** (Server 2022+)

### Production-Ready

- Comprehensive test suite (18K+ lines, conformance + integration tests)
- CI/CD with pinned dependencies for reproducible builds
- Extensive documentation and security guidelines
- Zero raw pointers - RAII throughout

## Quick Start

### Prerequisites

- CMake 3.16+
- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- vcpkg (for dependency management)
- Python 3.12+ (for codebase checks)

### Build

```bash
# Clone the repository
git clone https://github.com/itcv-GmbH/cpp-mcp-sdk.git
cd cpp-mcp-sdk

# Configure (macOS/Linux)
cmake --preset vcpkg-unix-release

# Configure (Windows)
cmake --preset vcpkg-windows-release

# Build
cmake --build build/vcpkg-unix-release        # macOS/Linux
cmake --build build/vcpkg-windows-release --config Release --parallel  # Windows
```

### Test

```bash
# Run all tests
ctest --test-dir build/vcpkg-unix-release

# Run specific test
ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_smoke_test -V
```

### Basic Example - MCP Server

```cpp
#include <mcp/server.hpp>
#include <mcp/transport/stdio.hpp>
#include <iostream>

auto main() -> int
{
  try
  {
    // Create server with stdio transport
    auto server = mcp::Server::create();
    
    // Register a tool
    server->registerTool(
      mcp::server::ToolDefinition{
        .name = "echo",
        .description = "Echo back the input message",
        .inputSchema = R"({
          "type": "object",
          "properties": {
            "message": {"type": "string"}
          },
          "required": ["message"]
        })"_json
      },
      [](const mcp::server::ToolCallContext& ctx) -> mcp::server::CallToolResult {
        auto message = ctx.arguments["message"].asString();
        return mcp::server::CallToolResult::success(
          mcp::server::ResourceContent::text("echo://result", message)
        );
      }
    );
    
    // Start server (blocks on stdio)
    server->start();
    
    return 0;
  }
  catch (const std::exception& e)
  {
    std::cerr << "Server error: " << e.what() << '\n';
    return 1;
  }
}
```

### Basic Example - MCP Client

```cpp
#include <mcp/client.hpp>
#include <mcp/transport/http.hpp>
#include <iostream>

auto main() -> int
{
  try
  {
    // Create client
    auto client = mcp::Client::create();
    
    // Connect to MCP server via HTTP
    client->connectHttp(mcp::transport::http::HttpClientOptions{
      .serverUrl = "http://localhost:8080/mcp"
    });
    
    // Initialize session
    auto initResponse = client->initialize().get();
    
    // List available tools
    auto toolsResult = client->listTools();
    std::cout << "Available tools: " << toolsResult.tools.size() << '\n';
    
    // Call a tool
    auto callResult = client->callTool("echo", {{"message", "Hello, MCP!"}});
    
    return 0;
  }
  catch (const std::exception& e)
  {
    std::cerr << "Client error: " << e.what() << '\n';
    return 1;
  }
}
```

## Installation

### Using vcpkg (Overlay Port)

```cmake
# In your CMakeLists.txt
find_package(mcp_sdk CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE mcp::sdk)
```

### Using CMake FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
  mcp_sdk
  GIT_REPOSITORY https://github.com/itcv-GmbH/cpp-mcp-sdk.git
  GIT_TAG        v0.1.0  # or specific commit
)

FetchContent_MakeAvailable(mcp_sdk)
target_link_libraries(your_target PRIVATE mcp::sdk)
```

## Documentation

- [API Overview](docs/api_overview.md) - High-level architecture
- [Server Quickstart](docs/quickstart_server.md) - Building MCP servers
- [Client Quickstart](docs/quickstart_client.md) - Building MCP clients
- [Security Guidelines](docs/security.md) - Security best practices
- [Dependencies](docs/dependencies.md) - Dependency management
- [Version Policy](docs/version_policy.md) - Protocol versioning strategy

## Examples

See [`examples/`](examples/) directory for complete working examples:

- `minimal_example.cpp` - Basic SDK usage
- `stdio_server/` - stdio transport server
- `dual_transport_server/` - Multi-transport server (stdio + HTTP)
- `http_listen_example/` - HTTP server with listen mode
- `bidirectional_sampling_elicitation/` - Client sampling and elicitation
- `http_server_auth/` - HTTP server with OAuth (requires TLS)
- `http_client_auth/` - HTTP client with authentication (requires TLS)
- `consumer_find_package/` - CMake consumer example
- `consumer_vcpkg_overlay/` - vcpkg overlay consumer example

## Architecture

```
include/mcp/
├── server/          # Server component (tools, resources, prompts)
├── client/          # Client component (roots, sampling, elicitation)
├── transport/       # Transports (stdio, Streamable HTTP)
├── jsonrpc/         # JSON-RPC 2.0 layer
├── auth/            # OAuth authorization
├── security/        # Security policies and limits
├── schema/          # JSON Schema validation
├── lifecycle/       # Session lifecycle management
└── util/            # Utilities (tasks, cancellation, progress)
```

## Testing

### Test Categories

- **Unit Tests**: Individual component testing
- **Conformance Tests**: MCP spec compliance verification
- **Integration Tests**: Cross-SDK interoperability
- **Feature Matrix Tests**: Build configuration variants

### Run Tests

```bash
# All tests
ctest --test-dir build/vcpkg-unix-release

# Conformance tests only
ctest --test-dir build/vcpkg-unix-release -L conformance

# Integration tests (requires MCP_SDK_INTEGRATION_TESTS=ON)
ctest --test-dir build/vcpkg-unix-release -L integration
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `MCP_SDK_BUILD_TESTS` | ON | Build test suite |
| `MCP_SDK_BUILD_EXAMPLES` | ON | Build examples |
| `BUILD_SHARED_LIBS` | OFF | Build shared libraries |
| `MCP_SDK_ENABLE_TLS` | ON | Enable TLS (requires OpenSSL) |
| `MCP_SDK_ENABLE_AUTH` | ON | Enable OAuth features |
| `MCP_SDK_INTEGRATION_TESTS` | OFF | Build integration tests |

## Compliance

This SDK is **fully compliant** with MCP specification 2025-11-25:

- ✅ Streamable HTTP transport (single endpoint, resumable SSE)
- ✅ OAuth 2.1 + PKCE authorization
- ✅ RFC9728 resource metadata discovery
- ✅ All server features (tools, resources, prompts)
- ✅ All client features (roots, sampling, elicitation)
- ✅ All utilities (ping, cancellation, progress, tasks)
- ✅ Cross-platform (Linux, macOS, Windows)

See the MCP specification in `.docs/requirements/mcp-spec-2025-11-25/` for detailed protocol requirements.

## Dependencies

Managed via vcpkg:

- [jsoncons](https://github.com/danielaparker/jsoncons) - JSON handling
- [Boost.Asio](https://www.boost.org/) - Async I/O
- [Boost.Beast](https://www.boost.org/) - HTTP/WebSocket
- [Boost.Process](https://www.boost.org/) - Process management
- [OpenSSL](https://www.openssl.org/) - TLS/crypto (optional)
- [Catch2](https://github.com/catchorg/Catch2) - Testing framework

## Contributing

1. Read [AGENTS.md](AGENTS.md) for development guidelines
2. Create a feature branch
3. Make changes following code style (clang-format/clang-tidy)
4. Run tests: `ctest --test-dir build/vcpkg-unix-release`
5. Run codebase checks: `cmake --build build/vcpkg-unix-release --target codebase-check`
6. Submit a pull request

### Code Quality

```bash
# Format code
cmake --build build/vcpkg-unix-release --target clang-format

# Check formatting
cmake --build build/vcpkg-unix-release --target clang-format-check

# Run all codebase checks
python3 tools/checks/run_checks.py
```

## License

Distributed under the **MIT License**. See [LICENSE](LICENSE) for details.

## Acknowledgments

- [Model Context Protocol](https://modelcontextprotocol.io/) specification team
- [vcpkg](https://vcpkg.io/) for dependency management
- [Catch2](https://github.com/catchorg/Catch2) for testing framework
