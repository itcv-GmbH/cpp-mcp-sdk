# C++ MCP SDK Implementation Plan (MCP 2025-11-25)

## ADR (Architecture Decision Record)

### ADR-001: Greenfield SDK in this repository
- Decision: Implement the SDK in this repository (currently docs-only).
- Rationale: Repo purpose per PM; enables deterministic pinned-spec conformance.

### ADR-002: Language + build baseline
- Decision: C++17 baseline; primary build system is CMake.
- Rationale: Required by SRS; maximizes platform/compiler support.

### ADR-003: Networking/HTTP/TLS stack
- Decision: Use Boost.Asio + Boost.Beast for HTTP client/server; OpenSSL for TLS on both client and server.
- Rationale: Cross-platform, production-proven; required HTTPS support with runtime-configurable server TLS.

### ADR-004: JSON + JSON-RPC modeling
- Decision: Use `jsoncons` as the single in-memory JSON model across the SDK and tests; implement explicit JSON-RPC 2.0 message types (request/notification/response) with strict validation.
- Rationale: The SDK must validate against JSON Schema 2020-12; `jsoncons` provides a JSON Schema implementation supporting draft 2020-12 and is available via vcpkg.

### ADR-005: JSON Schema validation
- Decision: Validate protocol messages and tool input/output schemas against the pinned MCP JSON Schema using `jsoncons` JSON Schema support (draft 2020-12).
- Rationale: SRS requires JSON Schema 2020-12 support; pinned mirror schema declares 2020-12; `jsoncons` explicitly supports draft 2020-12.

### ADR-006: Transport support
- Decision: Implement both required transports:
  - stdio (newline-delimited JSON)
  - Streamable HTTP (single endpoint; POST + GET SSE listen; resumability; session management)
- Rationale: Explicit MUSTs in SRS; required for interoperability.

### ADR-007: Bidirectional operation model
- Decision: Provide a shared bidirectional JSON-RPC session core used by both client and server roles, with:
  - handler registration per method
  - outbound request correlation
  - concurrent in-flight request support
  - timeout + cancellation + progress plumbing
- Rationale: MCP requires bidirectional requests and notifications.

### ADR-008: OAuth-based MCP Authorization (HTTP only)
- Decision: Implement MCP Authorization spec:
  - server: RFC9728 protected resource metadata publication + 401/403 challenges
  - client: RFC9728 discovery + RFC8414/OIDC discovery + OAuth 2.1 auth-code + PKCE(S256)
  - include loopback redirect receiver helper; do not include “open browser” helper (host app responsibility)
- Rationale: Required by SRS; loopback receiver needed for CLI/desktop UX.

### ADR-011: OAuth client registration approach
- Decision: Implement client-side support for:
  - pre-registered client credentials (host provided)
  - Client ID Metadata Documents (CIMD) when the host provides an HTTPS `client_id` URL
  - Dynamic Client Registration as a fallback when supported by the authorization server
- Rationale: MCP Authorization spec defines these approaches; SRS calls out CIMD and dynamic registration as SHOULD-level support.

### ADR-009: Legacy interop (older revisions)
- Decision: Treat legacy support as an optional follow-up milestone:
  - v1 ships full MCP 2025-11-25
  - vNext adds optional legacy 2024-11-05 HTTP+SSE fallback detection/client support
- Rationale: SRS says SHOULD interoperate with at least one older revision, but MCP 2025-11-25 changed HTTP transport semantics; implementing the legacy transport adds substantial scope.

### ADR-010: Dependency management via vcpkg (manifest mode) + future vcpkg port
- Decision: Use vcpkg (manifest mode) for third-party dependencies, pinned via `builtin-baseline`, and ensure the SDK’s CMake install/export layout is compatible with publishing as a vcpkg port later.
- Rationale: Deterministic builds and cross-platform dependency resolution are requirements; vcpkg ports require a `vcpkg.json` manifest and a conventional CMake install surface.

## Target Files (Planned Touch List)

Note: Paths are proposed; implementation may add more files but should stay within these areas.

### Build/Packaging
- `CMakeLists.txt`
- `cmake/mcp_sdkConfig.cmake.in`
- `cmake/Options.cmake`
- `cmake/Dependencies.cmake`
- `vcpkg.json`
- `vcpkg-configuration.json`
- `CMakePresets.json` (recommended)
- `vcpkg/ports/mcp-cpp-sdk/vcpkg.json` (overlay port; recommended)
- `vcpkg/ports/mcp-cpp-sdk/portfile.cmake` (overlay port; recommended)
- `vcpkg/ports/mcp-cpp-sdk/usage` (overlay port; recommended)
- `conanfile.py` (optional; not planned for v1)

### Public Headers (SDK API)
- `include/mcp/version.hpp`
- `include/mcp/errors.hpp`
- `include/mcp/jsonrpc/messages.hpp`
- `include/mcp/jsonrpc/router.hpp`
- `include/mcp/lifecycle/session.hpp`
- `include/mcp/schema/validator.hpp`
- `include/mcp/transport/stdio.hpp`
- `include/mcp/transport/http.hpp`
- `include/mcp/http/sse.hpp`
- `include/mcp/security/origin_policy.hpp`
- `include/mcp/security/limits.hpp`
- `include/mcp/server/server.hpp`
- `include/mcp/server/tools.hpp`
- `include/mcp/server/resources.hpp`
- `include/mcp/server/prompts.hpp`
- `include/mcp/server/utilities/logging.hpp`
- `include/mcp/server/utilities/completion.hpp`
- `include/mcp/server/utilities/pagination.hpp`
- `include/mcp/client/client.hpp`
- `include/mcp/client/roots.hpp`
- `include/mcp/client/sampling.hpp`
- `include/mcp/client/elicitation.hpp`
- `include/mcp/util/tasks.hpp`
- `include/mcp/util/cancellation.hpp`
- `include/mcp/util/progress.hpp`
- `include/mcp/auth/protected_resource_metadata.hpp`
- `include/mcp/auth/oauth_client.hpp`
- `include/mcp/auth/oauth_server.hpp`
- `include/mcp/auth/loopback_receiver.hpp`
- `include/mcp/auth/client_registration.hpp`

### Sources
- `src/jsonrpc/*.cpp`
- `src/lifecycle/*.cpp`
- `src/schema/*.cpp`
- `src/transport/*.cpp`
- `src/http/*.cpp`
- `src/security/*.cpp`
- `src/server/*.cpp`
- `src/client/*.cpp`
- `src/util/*.cpp`
- `src/auth/*.cpp`

### Tests (Conformance)
- `tests/CMakeLists.txt`
- `tests/conformance/test_pinned_mirror.cpp`
- `tests/conformance/test_schema_validation.cpp`
- `tests/conformance/test_lifecycle.cpp`
- `tests/conformance/test_jsonrpc_invariants.cpp`
- `tests/conformance/test_stdio_transport.cpp`
- `tests/conformance/test_streamable_http_transport.cpp`
- `tests/conformance/test_tasks.cpp`
- `tests/conformance/test_authorization.cpp`
- `tests/integration/*` (optional)

### Examples + Docs
- `examples/stdio_server/*`
- `examples/http_server_auth/*`
- `examples/stdio_client/*`
- `examples/http_client_auth/*`
- `examples/bidirectional_sampling_elicitation/*`
- `README.md`
- `docs/quickstart_server.md`
- `docs/quickstart_client.md`
- `docs/version_policy.md`
- `docs/security.md`

### CI
- `.github/workflows/ci.yml`

## Verification Strategy

- Dependency install: vcpkg manifest mode `vcpkg install` from repo root (creates `vcpkg_installed/` per-project).
- Build: CMake configure/build on Linux/macOS/Windows (GCC/Clang/MSVC) with C++17, using vcpkg toolchain integration.
- Unit/conformance tests: `ctest` for protocol invariants, schema validation, lifecycle ordering, tasks semantics, transport semantics, auth error semantics.
- Integration tests (recommended): run against at least one official SDK (TypeScript and/or Python) to validate Streamable HTTP + auth interoperability.
- Static checks (recommended): ASAN/UBSAN on Linux/macOS; MSVC warnings as errors.

## Scope Guardrails (Explicitly Out of Scope)

- Building a full MCP host UI application.
- Implementing an OAuth Authorization Server.
- Rendering icons or fetching icon bytes by default (SDK may provide policy helpers; host decides whether to fetch).
- Proprietary protocol extensions.
- A full HTTP framework abstraction layer beyond the built-in server/client required for conformance.

## Risks / Unknowns

- JSON Schema 2020-12 validator correctness vs MCP schema edge-cases (compile-time + runtime performance).
- Correct Streamable HTTP SSE resumability semantics (multi-stream routing + Last-Event-ID rules).
- OAuth security hardening (SSRF/redirect validation, token storage interface) across platforms.
- OAuth client registration complexity (CIMD hosting is app responsibility; dynamic registration support varies).
- Windows TLS and socket behavior differences under OpenSSL; CI needs real coverage.
- Tasks utility correctness (state transitions, TTL cleanup, auth-context binding).
- Backpressure/limits for SSE streams (resource exhaustion and retry loops).
- vcpkg baseline pinning and triplet differences may require explicit feature/option management for reproducibility.
