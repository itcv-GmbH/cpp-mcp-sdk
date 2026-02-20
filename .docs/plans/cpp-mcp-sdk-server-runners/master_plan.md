# C++ MCP SDK: Server Runners (STDIO + Streamable HTTP)

Add first-class convenience runners for implementing MCP servers over STDIO and Streamable HTTP without requiring users to hand-wire message loops, HTTP runtime plumbing, or risk writing non-protocol output to STDOUT.

This plan is driven by the SRS requirements to provide APIs to run a server over STDIO and Streamable HTTP, and to be production-usable for C++ application developers.

## ADR (Architecture Decision Record)

### ADR-SR-001: Provide server runners as opt-in orchestration utilities
- Decision: Introduce new server-facing runner utilities that compose existing `mcp::Server`, `mcp::transport::StdioTransport` (static helpers), `mcp::transport::http::StreamableHttpServer`, and `mcp::transport::HttpServerRuntime`.
- Rationale: The SDK already implements correct protocol semantics; the missing piece is ergonomic orchestration. Runners improve DX while preserving layering and conformance.

### ADR-SR-002: Use a `ServerFactory` to support multiple HTTP sessions safely
- Decision: Runner APIs accept a `ServerFactory` callback that creates a fully-configured `std::shared_ptr<mcp::Server>` instance.
- Rationale: `mcp::Session` lifecycle state is per-connection; sharing a single `mcp::Server` across multiple HTTP sessions would conflate lifecycle state. A factory enables correct session isolation and also enables running STDIO + HTTP concurrently.

### ADR-SR-003: Runners never write logs to STDOUT
- Decision: STDIO runner writes only newline-delimited MCP JSON-RPC messages to the configured output stream; all diagnostics go to a dedicated error stream (default `std::cerr`) or are suppressed.
- Rationale: MCP stdio transport forbids non-protocol writes to STDOUT. A runner should make the safe path the default.

### ADR-SR-004: Keep protocol behavior semantics-preserving
- Decision: Runners must be thin wiring around existing `mcp::Server` and transport implementations; they must not reinterpret protocol semantics (timeouts, message ordering, error codes).
- Rationale: Conformance tests and pinned spec mirror are the source of truth; runners are purely ergonomics.

### ADR-SR-005: Provide an optional combined runner for multi-transport servers
- Decision: Provide an additional convenience wrapper that can start STDIO, Streamable HTTP, or both, using the same `ServerFactory`.
- Rationale: Some deployments need a single process that serves both (local stdio for desktop hosts and HTTP for remote clients). A combined runner removes orchestration boilerplate while still allowing users to start individual runners directly.

## Scope Guardrails (Explicitly Out of Scope)

- Replacing or reintroducing stub transport classes (e.g., the removed `HttpTransport`).
- Changing protocol semantics, schema validation rules, authorization behavior, or lifecycle behavior.
- Introducing new third-party dependencies.
- Adding a generic cross-transport abstraction layer beyond simple orchestration helpers.

## Target Files (Planned Touch List)

### SRS / Spec Inputs (Read-Only; normative)
- `.docs/requirements/cpp-mcp-sdk.md` (Transport Support; Documentation)
- `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
- `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`

### New Public Headers (to be added)
- `include/mcp/server/runners.hpp` (umbrella include)
- `include/mcp/server/stdio_runner.hpp`
- `include/mcp/server/streamable_http_runner.hpp`
- `include/mcp/server/combined_runner.hpp`

### Production Code (expected to change)
- `src/server/stdio_runner.cpp`
- `src/server/streamable_http_runner.cpp`
- `src/server/combined_runner.cpp`
- `docs/api_overview.md` (document new runner utilities)
- `docs/quickstart_server.md` (show runner usage)

### Examples / Docs (expected to change)
- `examples/stdio_server/main.cpp` (migrate to runner; reduce boilerplate)
- `examples/http_server_auth/main.cpp` (migrate to runner; preserve auth demo)
- `examples/dual_transport_server/main.cpp` (new; demonstrates STDIO + HTTP concurrently)

### Tests / Build Wiring (expected to change)
- `tests/CMakeLists.txt`
- `tests/server_stdio_runner_test.cpp` (new)
- `tests/server_streamable_http_runner_test.cpp` (new)
- `tests/server_combined_runner_test.cpp` (new)

## Verification Strategy

- Unit tests for STDIO runner using `std::istringstream`/`std::ostringstream` to verify:
  - only MCP messages written to output
  - parse errors emit JSON-RPC parse error response with `id: null`
  - embedded newlines are rejected/never emitted

- Unit tests for HTTP runner verifying:
  - per-session isolation (two sessions can initialize without interfering)
  - server-initiated messages route to `enqueueServerMessage(..., sessionId)`

- Unit tests for combined runner verifying:
  - stdio-only and http-only modes work
  - start HTTP then run STDIO does not hang and shuts down cleanly

- Full gate:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`

## Risks / Unknowns

- API surface choices (namespaces/types) must align with existing public headers and semver policy.
- HTTP runner session lifecycle: mapping when sessions are created/expired must remain consistent with `StreamableHttpServer` behavior.
- Threading/shutdown: runner async helpers must not introduce hangs or detached-thread leaks.
- Combined runner stop semantics for STDIO: stopping a blocking read on `std::cin` is inherently host-driven (EOF). The API must document this clearly.
- Examples migration must preserve documentation expectations and not regress quickstart instructions.
