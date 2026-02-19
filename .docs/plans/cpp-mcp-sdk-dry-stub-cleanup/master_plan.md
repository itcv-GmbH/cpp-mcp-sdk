# cpp-mcp-sdk DRY + Stub API Cleanup

Reduce duplicated parsing/encoding helpers, remove or hard-disable misleading stub APIs, and fix a known Streamable HTTP server deadlock risk. This work is intentionally semantics-preserving for MCP protocol behavior (per the SRS) except where we replace silent no-ops with explicit failures.

## ADR (Architecture Decision Record)

### ADR-QC-001: Centralize shared helpers as internal headers
- Decision: Introduce internal helper headers under `include/mcp/detail/` for ASCII utilities, base64url (no padding), absolute URL parsing, and initialize/capabilities JSON codec helpers.
- Rationale: Multiple modules re-implement the same primitives (often security-critical). Centralization reduces drift and makes it easier to test/lock behavior.

### ADR-QC-002: Prefer loud failure over silent stub behavior
- Decision: Remove or deprecate-and-throw APIs that look functional but are stubs (e.g., `transport::HttpTransport` dropping messages).
- Rationale: Silent message loss is a production hazard and undermines conformance troubleshooting.

### ADR-QC-003: Clarify layering: Session is lifecycle state, Router is request correlation
- Decision: Make `mcp::Session` lifecycle enforcement explicit and remove misleading TODOs that imply it performs transport-backed request sending.
- Rationale: The SDK’s actual request/response correlation lives in `jsonrpc::Router` (used by `Client`/`Server`). Keeping responsibilities clear reduces “API slop.”

### ADR-QC-004: Do not hold StreamableHttpServer locks while invoking user handlers
- Decision: Refactor `transport::http::StreamableHttpServer` so internal mutexes are not held across calls into user-provided request/notification/response handlers.
- Rationale: Prevent deadlocks/re-entrancy issues and improve throughput under concurrent traffic.

### ADR-QC-005: Replace detached threads with managed execution (optional)
- Decision: Where the SDK currently uses `std::thread(...).detach()` for request handling or callbacks, replace with a managed worker pool or a joinable thread set that is drained on shutdown.
- Rationale: Detached threads complicate shutdown and can cause hangs or resource leaks in long-running hosts.

## Target Files (Planned Touch List)

### SRS / Spec Inputs (Read-Only; normative)
- `.docs/requirements/cpp-mcp-sdk.md`
- `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
- `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`
- `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
- `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`

### New Internal Headers (to be added)
- `include/mcp/detail/ascii.hpp`
- `include/mcp/detail/base64url.hpp`
- `include/mcp/detail/url.hpp`
- `include/mcp/detail/initialize_codec.hpp`

### Production Code (expected to change)
- `include/mcp/security/origin_policy.hpp`
- `include/mcp/transport/http.hpp`
- `include/mcp/transport/stdio.hpp`
- `include/mcp/lifecycle/session.hpp`
- `src/auth_oauth_client.cpp`
- `src/auth_protected_resource_metadata.cpp`
- `src/auth_client_registration.cpp`
- `src/auth_loopback_receiver.cpp`
- `src/client/client.cpp`
- `src/lifecycle/session.cpp`
- `src/server/server.cpp`
- `src/transport/http_client.cpp`
- `src/transport/http_runtime.cpp`
- `src/transport/http_server.cpp`
- `src/transport/stdio.cpp`
- `src/util/tasks.cpp`
- `src/jsonrpc/router.cpp` (optional: detached thread cleanup)
- `include/mcp/jsonrpc/router.hpp` (optional: thread/executor options)

### Tests / Build Wiring (expected to change)
- `tests/CMakeLists.txt`
- `tests/detail_ascii_test.cpp`
- `tests/detail_base64url_test.cpp`
- `tests/detail_url_test.cpp`
- `tests/detail_initialize_codec_test.cpp`
- `tests/lifecycle_test.cpp`
- `tests/transport_http_common_test.cpp`
- `tests/smoke_test.cpp` (if API surface changes)

## Verification Strategy

- Per-task: run the most relevant test executable(s) via CTest regex (example):
  - `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_oauth_client_test_authorization --output-on-failure`

- Full gate (required before merge):
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`

- Conformance must remain green:
  - `mcp_sdk_conformance_streamable_http_transport_test`
  - `mcp_sdk_conformance_authorization_test_authorization`
  - `mcp_sdk_conformance_tasks_test`
  - `mcp_sdk_conformance_lifecycle_test`

- If optional threading work is performed:
  - `mcp_sdk_jsonrpc_router_test`
  - `mcp_sdk_client_test`

## Scope Guardrails (Explicitly Out of Scope)

- Adding new MCP protocol features or changing spec-defined semantics.
- Introducing new third-party dependencies.
- Re-architecting transport plumbing beyond disabling/removing stubs and fixing handler-locking.
- Threading changes that alter externally observable protocol semantics (timeouts, cancellation ordering, message ordering). Threading work is limited to replacing detached threads with managed execution.
- Performance work not required to preserve existing behavior (except avoiding pathological lock contention from handler invocation under lock).

## Risks / Unknowns

- URL parsing consolidation is security-sensitive (SSRF/redirect rules). Use golden test vectors and rely on existing authorization conformance tests as a backstop.
- ASCII case-folding must remain locale-independent (HTTP header matching is ASCII). Ensure helper implementations use `unsigned char` + `std::tolower`/`std::isspace` correctly.
- Removing or deprecating public stub classes is an API surface change; confirm versioning/semver policy for this SDK.
- Deadlock regression tests must avoid hanging CI; use `std::async` + `wait_for` timeouts and fail fast.
- Threading refactors are high-risk for shutdown ordering. Keep changes incremental and add “no-hang on destruction/stop” tests.
