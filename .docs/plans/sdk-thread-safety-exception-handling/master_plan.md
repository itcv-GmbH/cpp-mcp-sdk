# SDK Thread Safety And Exception Handling - Implementation Plan

## Architecture Decision Record (ADR)

- The SDK will define an explicit thread-safety contract for every public type and public entrypoint that participates in concurrency, lifecycle management, background execution, or callback invocation.
- The SDK will define an explicit exception contract for every public type and public entrypoint that participates in concurrency, lifecycle management, background execution, or callback invocation.
- All background execution contexts created by the SDK (including `std::thread` entrypoints, `mcp::detail::InboundLoop` threads, and work posted to `boost::asio::thread_pool`) will enforce a no-throw boundary and will report failures through a unified error reporting mechanism.
- The SDK will add deterministic tests that validate:
  - no exceptions escape thread entrypoints
  - no exceptions escape destructors and `stop()` functions declared `noexcept`
  - concurrent request routing remains correct under multi-threaded load

## Target Files

- `include/mcp/client/client.hpp`
- `include/mcp/server/server.hpp`
- `include/mcp/server/combined_runner.hpp`
- `include/mcp/server/streamable_http_runner.hpp`
- `include/mcp/server/stdio_runner.hpp`
- `include/mcp/jsonrpc/router.hpp`
- `include/mcp/jsonrpc/messages.hpp`
- `include/mcp/lifecycle/session.hpp`
- `include/mcp/detail/inbound_loop.hpp`
- `include/mcp/transport/transport.hpp`
- `include/mcp/transport/http.hpp`
- `include/mcp/transport/stdio.hpp`
- `include/mcp/transport/streamable_http_client_transport.hpp`
- `include/mcp/errors.hpp`
- `src/client/client.cpp`
- `src/server/server.cpp`
- `src/server/combined_runner.cpp`
- `src/server/streamable_http_runner.cpp`
- `src/server/stdio_runner.cpp`
- `src/lifecycle/session.cpp`
- `src/jsonrpc/router.cpp`
- `src/detail/inbound_loop.cpp`
- `src/transport/streamable_http_client_transport.cpp`
- `src/transport/http_client.cpp`
- `src/transport/http_runtime.cpp`
- `src/transport/http_server.cpp`
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
- `std::function` does not encode `noexcept` in its type. All error reporting callback invocation sites will be required to catch all exceptions and will be required to suppress callback failures.
- Adding error reporting and tightening exception containment will change observable behavior for callers whose callbacks currently throw. The exception contract will be required to define the containment and reporting rules for all user-provided callbacks.
