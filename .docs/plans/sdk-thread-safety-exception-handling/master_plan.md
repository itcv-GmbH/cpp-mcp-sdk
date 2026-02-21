# SDK Thread Safety And Exception Handling - Implementation Plan

## Architecture Decision Record (ADR)

- The SDK will define an explicit thread-safety contract for each public type and each public entrypoint.
- The SDK will define an explicit exception contract for each public type and each public entrypoint.
- All background threads created by the SDK will enforce a no-throw boundary and will report failures through a unified error reporting mechanism.
- The SDK will add deterministic tests that validate:
  - no exceptions escape thread entrypoints
  - no exceptions escape destructors and `stop()` functions declared `noexcept`
  - concurrent request routing remains correct under multi-threaded load

## Target Files

- `.docs/requirements/cpp-mcp-sdk.md`
- `include/mcp/client/client.hpp`
- `include/mcp/server/server.hpp`
- `include/mcp/server/combined_runner.hpp`
- `include/mcp/server/streamable_http_runner.hpp`
- `include/mcp/server/stdio_runner.hpp`
- `include/mcp/jsonrpc/router.hpp`
- `include/mcp/lifecycle/session.hpp`
- `include/mcp/transport/transport.hpp`
- `include/mcp/transport/http.hpp`
- `include/mcp/transport/stdio.hpp`
- `src/client/client.cpp`
- `src/server/server.cpp`
- `src/server/combined_runner.cpp`
- `src/server/streamable_http_runner.cpp`
- `src/server/stdio_runner.cpp`
- `src/jsonrpc/router.cpp`
- `src/transport/http_client.cpp`
- `src/transport/http_runtime.cpp`
- `src/transport/stdio.cpp`
- `tests/`

## Verification Strategy

- Unit tests will validate thread-safety and exception boundary invariants.
- Tests will include concurrent stress tests for request routing and transport start-stop sequencing.
- The implementation will be verified by running:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release`

## Scope Guardrails

- This plan will not change the MCP protocol surface.
- This plan will not change JSON-RPC message schemas.
- This plan will not add new third-party dependencies.
- This plan will not introduce a new threading runtime beyond the existing std::thread and Boost.Asio thread pools already used by the SDK.

## Risks / Unknowns

- Some public entrypoints currently lack explicit documentation for threading and exceptions. The contract work will require an audit of every public header.
- Some modules currently suppress exceptions without reporting. The new error reporting mechanism will require consistent integration across transports, runners, and JSON-RPC routing.
- Multi-threaded tests risk flakiness. The test suite will require deterministic synchronization and bounded timeouts.
