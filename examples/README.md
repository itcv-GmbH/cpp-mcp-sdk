# MCP C++ SDK Examples

This directory contains complete, runnable examples demonstrating how to use the MCP C++ SDK for various use cases.

## Server Examples

- **[`stdio_server/`](stdio_server/)**: A basic MCP server using the stdio transport. It registers tools, resources, and prompts, and runs synchronously in the foreground.
- **[`http_listen_example/`](http_listen_example/)**: Demonstrates how to run an in-process Streamable HTTP server that uses the GET SSE listen feature to support server-initiated messages (like `roots/list`).
- **[`http_server_auth/`](http_server_auth/)**: An advanced HTTP server that enforces HTTPS (TLS) and implements OAuth 2.1 bearer token authentication with scope-based authorization.
- **[`dual_transport_server/`](dual_transport_server/)**: Shows how to run both stdio and HTTP transports simultaneously in a single server process, sharing the same tools and configuration.

## Client Examples

- **[`http_client_auth/`](http_client_auth/)**: A client that connects to an OAuth-protected HTTP server. It demonstrates metadata discovery, PKCE flow, and capturing a loopback redirect to get an authorization code.
- **[`bidirectional_sampling_elicitation/`](bidirectional_sampling_elicitation/)**: A client that demonstrates server-initiated requests, specifically handling `sampling/createMessage` and both form and URL `elicitation/create` requests.

## Consumer Integration Examples

These examples demonstrate how external CMake projects can consume the SDK:

- **[`consumer_find_package/`](consumer_find_package/)**: How to integrate the SDK into an external project using standard CMake `find_package()`.
- **[`consumer_vcpkg_overlay/`](consumer_vcpkg_overlay/)**: How to consume the SDK using vcpkg manifest mode with a custom local overlay port.

## Minimal Examples

- **[`minimal_example.cpp`](minimal_example.cpp)**: A single-file demonstration showing the absolute minimum code required to instantiate and start an MCP server and client.

## Building the Examples

All examples are built automatically when configuring the main SDK project with `MCP_SDK_BUILD_EXAMPLES=ON` (which is the default).

```bash
# From the project root (macOS/Linux)
cmake --preset vcpkg-unix-release
cmake --build build/vcpkg-unix-release

# The compiled examples will be located in:
# build/vcpkg-unix-release/examples/
```

For detailed instructions on running each example, see the individual `README.md` files inside each example directory.