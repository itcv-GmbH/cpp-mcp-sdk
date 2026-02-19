# cpp-mcp-sdk Unit Test Expansion

Increase unit test depth and breadth for the C++ MCP SDK, prioritizing high-risk protocol/security areas (auth, transports, lifecycle/router/tasks) while keeping tests deterministic and cross-platform.

## ADR (Architecture Decision Record)

### ADR-UT-001: Keep current Catch2 + multi-executable layout
- Decision: Continue the existing pattern of one `tests/*.cpp` -> one `mcp_sdk_test_*` executable linked against `Catch2::Catch2WithMain`.
- Rationale: Minimizes churn and merge risk; aligns with existing `tests/CMakeLists.txt` conventions.

### ADR-UT-002: Deterministic, in-process-first tests
- Decision: Prefer in-process fixtures (in-memory stdio streams; Streamable HTTP server/client with injected request executors) over real sockets and sleeps.
- Rationale: Reduces flakiness and improves Windows/macOS/Linux parity.

### ADR-UT-003: Add direct tests for header-only helper contracts
- Decision: Add unit tests for parsing/building helpers in `include/mcp/util/*`, schema pinning accessors, version helpers, and limits.
- Rationale: These helpers are security-critical (parsing) and high-leverage; direct tests prevent regressions not covered by conformance flows.

### ADR-UT-004: Optional CI feature-matrix builds (auth-off / no-openssl)
- Decision: Add an optional CI lane to compile and run a compatible subset of tests with `MCP_SDK_ENABLE_AUTH=OFF` and/or `MCP_SDK_ENABLE_TLS=OFF`.
- Rationale: Validates supported build configurations and ensures disabled-feature stubs remain correct.

## Target Files (Planned Touch List)

### Test Build Wiring
- `tests/CMakeLists.txt`

### New Unit Test Files (to be added)
- `tests/util_cancellation_test.cpp`
- `tests/util_progress_test.cpp`
- `tests/schema_pinned_schema_test.cpp`
- `tests/sdk_version_test.cpp`
- `tests/security_crypto_random_test.cpp`
- `tests/security_limits_test.cpp`
- `tests/transport_http_runtime_test.cpp`
- `tests/auth_oauth_client_disabled_test.cpp`

### Existing Unit Test Files (to be expanded)
- `tests/schema_validator_test.cpp`
- `tests/jsonrpc_messages_test.cpp`
- `tests/jsonrpc_router_test.cpp`
- `tests/lifecycle_test.cpp`
- `tests/tasks_test.cpp`
- `tests/transport_stdio_test.cpp`
- `tests/transport_stdio_subprocess_test.cpp`
- `tests/transport_http_common_test.cpp`
- `tests/transport_http_client_test.cpp`
- `tests/transport_http_server_test.cpp`
- `tests/transport_http_tls_test.cpp`
- `tests/auth_protected_resource_metadata_test.cpp`
- `tests/auth_client_registration_test.cpp`
- `tests/auth_oauth_client_test.cpp`
- `tests/client_test.cpp`
- `tests/server_test.cpp`

### Conformance Tests (only if needed to keep suite compatible)
- `tests/conformance/test_streamable_http_transport.cpp`
- `tests/conformance/test_authorization.cpp`

### CI (Optional)
- `.github/workflows/ci.yml`
- `CMakePresets.json` (optional: add named presets for feature variants)

## Verification Strategy

- Local build + run all tests:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`

- Focused verification per task:
  - Use `ctest --test-dir build/vcpkg-unix-release -R <regex> --output-on-failure` to run the relevant test executable(s).

- Optional feature-variant verification:
  - Configure a separate build directory with `-DMCP_SDK_ENABLE_AUTH=OFF` and/or `-DMCP_SDK_ENABLE_TLS=OFF` and run the allowed test subset.

## Scope Guardrails (Explicitly Out of Scope)

- Modifying SDK runtime behavior (production code) to “make tests pass” unless a separate bugfix plan is created.
- Refactoring existing test files for style-only reasons.
- Introducing new third-party dependencies (beyond Catch2 already used).
- Adding code coverage tooling (lcov/gcovr) unless explicitly requested.

## Risks / Unknowns

- Some APIs are intentionally async/time-based; tests must avoid sleep-based assertions to prevent flakiness.
- Feature-matrix builds (auth-off / tls-off) may require conditional compilation or selective test execution to avoid false failures.
- There are known TODOs in `src/lifecycle/session.cpp` around transport-backed request sending and shutdown; avoid writing tests that assume unimplemented behavior.
