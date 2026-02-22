# C++ MCP SDK Codebase Cleanup (Non-Functional Refactor)

This plan is responsible for restructuring the repository for navigability and hygiene while preserving externally observable SDK behavior.

## ADR (Architecture Decision Record)

### ADR-CC-001: Public include stability via umbrella headers
- Decision: Every existing public header path that currently exposes multiple top-level `class` and `struct` definitions will become an umbrella header at the same include path.
- Decision: Every public `class` and `struct` type will live in exactly one dedicated per-type header whose basename matches the type name in `snake_case`.
- Decision: Umbrella headers will contain zero `class` declarations and zero `struct` declarations and will re-export types by including per-type headers.
- Rationale: The SDK is required to keep existing include paths valid while enforcing a one-type-per-header public include layout.

### ADR-CC-002: Internal implementation layout remains module-aligned
- Decision: Implementation sources will live under `src/<module>/...` and `src/` root will contain zero domain-prefixed implementation files.
- Rationale: The SRS requires module-aligned source placement and forbids domain-prefixed files in `src/` root.

### ADR-CC-003: Deterministic repository enforcement checks
- Decision: The repository will include deterministic, automated checks for:
  - one top-level `class` or `struct` definition per file under `include/mcp/`
  - zero directory-traversing relative includes in `src/` and `tests/`
- Decision: CI will execute the enforcement checks.
- Rationale: The SRS requires enforcement checks and CI execution.

### ADR-CC-004: Public detail header for threading boundary
- Decision: `src/detail/thread_boundary.hpp` will relocate into `include/mcp/detail/thread_boundary.hpp`.
- Decision: All call sites will include it via `<mcp/detail/thread_boundary.hpp>`.
- Rationale: The SRS requires the threading boundary header to live under the public include tree and to be included via `<mcp/detail/...>`.

## Target Files (Planned Touch List)

### SRS Input (Read-Only; normative)
- `.docs/requirements/cpp-sdk-codebase-cleanup.md`

### Repository Hygiene
- `.gitignore`

### Build System
- `CMakeLists.txt`

### CI
- `.github/workflows/ci.yml`

### Enforcement Scripts (New)
- `tools/checks/check_public_header_one_type.py`
- `tools/checks/check_include_policy.py`
- `tools/checks/check_git_index_hygiene.py`
- `tools/checks/run_checks.py`

### Implementation File Relocations
- `src/auth_client_registration.cpp` -> `src/auth/client_registration.cpp`
- `src/auth_loopback_receiver.cpp` -> `src/auth/loopback_receiver.cpp`
- `src/auth_oauth_client.cpp` -> `src/auth/oauth_client.cpp`
- `src/auth_oauth_client_disabled.cpp` -> `src/auth/oauth_client_disabled.cpp`
- `src/auth_protected_resource_metadata.cpp` -> `src/auth/protected_resource_metadata.cpp`

### Thread Boundary Relocation
- `src/detail/thread_boundary.hpp` -> `include/mcp/detail/thread_boundary.hpp`
- Call sites:
  - `src/jsonrpc/router.cpp`
  - `src/server/server.cpp`
  - `src/server/stdio_runner.cpp`
  - `tests/detail/thread_boundary_test.cpp`

### Public Header Splits (Umbrella Conversions)
- `include/mcp/transport/http.hpp`
- `include/mcp/util/tasks.hpp`
- `include/mcp/auth/oauth_client.hpp`
- `include/mcp/auth/client_registration.hpp`
- `include/mcp/auth/loopback_receiver.hpp`

### Public Header Splits (Additional Files Required by One-Type Rule)
- `include/mcp/auth/protected_resource_metadata.hpp`
- `include/mcp/auth/oauth_server.hpp`
- `include/mcp/auth/provider.hpp`
- `include/mcp/lifecycle/session.hpp`
- `include/mcp/jsonrpc/messages.hpp`
- `include/mcp/jsonrpc/router.hpp`
- `include/mcp/client/client.hpp`
- `include/mcp/client/elicitation.hpp`
- `include/mcp/client/roots.hpp`
- `include/mcp/transport/stdio.hpp`
- `include/mcp/server/server.hpp`
- `include/mcp/server/resources.hpp`
- `include/mcp/server/prompts.hpp`
- `include/mcp/server/tools.hpp`
- `include/mcp/server/combined_runner.hpp`
- `include/mcp/server/stdio_runner.hpp`
- `include/mcp/server/streamable_http_runner.hpp`
- `include/mcp/schema/validator.hpp`
- `include/mcp/http/sse.hpp`
- `include/mcp/security/origin_policy.hpp`

### Public Header Basename Normalization (Umbrella Conversions)
- `include/mcp/error_reporter.hpp`
- `include/mcp/errors.hpp`
- `include/mcp/version.hpp`
- `include/mcp/security/limits.hpp`
- `include/mcp/util/cancellation.hpp`
- `include/mcp/util/progress.hpp`
- `include/mcp/detail/url.hpp`
- `include/mcp/client/sampling.hpp`

### Tests
- `tests/detail/thread_boundary_test.cpp`

## Verification Strategy

- Per-task verification is required to run the enforcement scripts:
  - `python3 tools/checks/run_checks.py`
- Per-task verification is required to build and run unit tests for impacted modules:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`
- Full gate verification is required to run formatting checks:
  - `cmake --build build/vcpkg-unix-release --target clang-format-check`

## Scope Guardrails (Out of Scope)

- Protocol semantics, transport semantics, authorization semantics, validation semantics, wire formats, HTTP headers, defaults, timeouts, and error codes are required to remain unchanged.
- New externally visible SDK features are not included.
- Behavioral changes are not included.

## Risks / Unknowns

- Public header splitting is required to preserve ABI and API meaning, which requires exact preservation of declarations, namespaces, defaults, and inline definitions.
- Umbrella header conversion is required to avoid introducing include cycles; per-type headers are required to maintain minimal and correct include dependencies.
- Enforcement script parsing is required to avoid false positives by excluding `enum class` and nested type definitions.
