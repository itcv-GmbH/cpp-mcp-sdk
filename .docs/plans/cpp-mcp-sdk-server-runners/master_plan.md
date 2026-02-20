# C++ MCP SDK: Server Runners (STDIO + Streamable HTTP)

Add first-class convenience runners for implementing MCP servers over STDIO and Streamable HTTP without requiring users to hand-wire message loops, HTTP runtime plumbing, or risk writing non-protocol output to STDOUT.

This plan is driven by the SRS requirements to provide APIs to run a server over STDIO and Streamable HTTP, and to be production-usable for C++ application developers.

## ADR (Architecture Decision Record)

### ADR-SR-001: Provide server runners as opt-in orchestration utilities
- Decision: Introduce new server-facing runner utilities that compose existing `mcp::Server`, `mcp::transport::StdioTransport` (static helpers), `mcp::transport::http::StreamableHttpServer`, and `mcp::transport::HttpServerRuntime`.
- Rationale: The SDK already implements correct protocol semantics; the missing piece is ergonomic orchestration. Runners improve DX while preserving layering and conformance.

### ADR-SR-002: Use a `ServerFactory` to support multiple HTTP sessions safely
- Decision: Streamable HTTP runner uses a `ServerFactory` to create a fully-configured `std::shared_ptr<mcp::Server>` **per HTTP session** (keyed by `MCP-Session-Id`).
- Rationale: MCP lifecycle state is per session; Streamable HTTP servers will handle multiple client connections. A runner must not route multiple sessions through a single `mcp::Server` instance.

### ADR-SR-003: Make Streamable HTTP session management spec-conformant by issuing `MCP-Session-Id`
- Decision: Update `mcp::transport::http::StreamableHttpServer` to issue a cryptographically secure, visible-ASCII `MCP-Session-Id` on successful initialize responses when `HttpServerOptions.requireSessionId=true`.
- Rationale: MCP Streamable HTTP session management (spec) requires the session ID to be assigned at initialization time via the response containing `InitializeResult`. Without a server-issued session ID, true multi-client session isolation cannot be achieved.

### ADR-SR-004: Runners never write logs to STDOUT
- Decision: STDIO runner writes only newline-delimited MCP JSON-RPC messages to the configured output stream; all diagnostics go to a dedicated error stream (default `std::cerr`) or are suppressed.
- Rationale: MCP stdio transport forbids non-protocol writes to STDOUT. A runner must make the safe path the default.

### ADR-SR-005: Keep protocol behavior semantics-preserving
- Decision: Runners must be thin wiring around existing `mcp::Server` and transport implementations; they must not reinterpret protocol semantics (timeouts, message ordering, error codes).
- Rationale: Conformance tests and pinned spec mirror are the source of truth; runners are purely ergonomics.

### ADR-SR-006: Provide a combined runner for multi-transport servers
- Decision: Provide an additional convenience wrapper that will support starting STDIO, Streamable HTTP, or both, using the same `ServerFactory`.
- Rationale: Some deployments require a single process that serves both (local stdio for desktop hosts and HTTP for remote clients). A combined runner removes orchestration boilerplate while preserving direct use of the individual runners.

## Scope Guardrails (Explicitly Out of Scope)

- Replacing or reintroducing stub transport classes (e.g., the removed `HttpTransport`).
- Changing protocol semantics beyond spec-conformant Streamable HTTP session handling (this plan only adds missing `MCP-Session-Id` issuance on initialize when sessions are required).
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
- `src/transport/http_server.cpp` (issue session IDs on initialize; session bookkeeping)
- `docs/api_overview.md` (document new runner utilities)
- `docs/quickstart_server.md` (show runner usage)

### Examples / Docs (expected to change)
- `examples/stdio_server/main.cpp` (migrate to runner; reduce boilerplate)
- `examples/http_server_auth/main.cpp` (migrate to runner; preserve auth demo)
- `examples/dual_transport_server/main.cpp` (new; demonstrates STDIO + HTTP concurrently)

### Tests / Build Wiring (expected to change)
- `tests/CMakeLists.txt`
- `tests/conformance/test_streamable_http_transport.cpp` (extend to assert session issuance when required)
- `tests/server_stdio_runner_test.cpp` (new)
- `tests/server_streamable_http_runner_test.cpp` (new)
- `tests/server_combined_runner_test.cpp` (new)

### Integration / Reference SDK Interop (expected to change)
- `tests/integration/cpp_server_fixture.cpp` (migrate to Streamable HTTP runner; remove manual session header injection)
- `tests/integration/scripts/reference_client_to_cpp_server.py` (assert server-issued `MCP-Session-Id` on successful initialize)
- `tests/integration/README.md` (update coverage text to reference runner-based fixtures)
- `tests/integration/cpp_stdio_server_fixture.cpp` (new; runner-based STDIO server fixture)
- `tests/integration/scripts/reference_client_to_cpp_stdio_server.py` (new; reference Python client drives C++ STDIO runner)
- `tests/integration/CMakeLists.txt` (add stdio fixture target and python-driven test)

## Verification Strategy

- Unit tests for STDIO runner using `std::istringstream`/`std::ostringstream` to verify:
  - only MCP messages written to output
  - parse errors emit JSON-RPC parse error response with `id: null`
  - embedded newlines are rejected/never emitted

- Unit tests for HTTP runner verifying:
  - per-session isolation (two sessions will initialize without interfering; factory invoked twice)
  - server-initiated messages route to `enqueueServerMessage(..., sessionId)`

- Transport conformance coverage (extend existing test):
  - when `requireSessionId=true`, initialize responses include `MCP-Session-Id`
  - missing required session IDs return HTTP 400 for non-initialize requests
  - DELETE terminates sessions and further requests return HTTP 404

- Unit tests for combined runner verifying:
  - stdio-only and http-only modes work
  - start HTTP then run STDIO does not hang and shuts down cleanly

- Full gate:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`

- Integration gate (reference Python SDK interop):
  - `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release -R integration_reference --output-on-failure`

## Risks / Unknowns

- API surface choices (namespaces/types) must align with existing public headers and semver policy.
- Transport change risk: implementation is required to update conformance tests and examples that assert initialize HTTP response headers.
- Session cleanup: runner must drop per-session servers on HTTP DELETE and on transport 404 (expired/terminated sessions) to avoid leaks.
- `HttpServerRuntime` currently serves requests synchronously on a single server thread; future work is required to evaluate concurrency scaling under high fan-in workloads.
- Threading/shutdown: runner async helpers must not introduce hangs or detached-thread leaks.
- Combined runner stop semantics for STDIO: stopping a blocking read on `std::cin` is inherently host-driven (EOF). The API must document this clearly.
- Examples migration must preserve documentation expectations and not regress quickstart instructions.
- Integration environment: the integration suite requires a Python interpreter and the pinned reference SDK venv provisioning used by `tests/integration/`.
