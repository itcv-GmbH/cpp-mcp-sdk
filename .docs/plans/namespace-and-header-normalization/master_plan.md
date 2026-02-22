# Namespace And Header Normalization (Breaking Refactor)

This plan is responsible for normalizing the public include tree under `include/mcp/` so that:
- namespaces mirror directory layout
- per-type headers and umbrella headers are deterministic
- `namespace detail` usage is consistent and non-redundant
- a deterministic enforcement check runs in CI

## ADR (Architecture Decision Record)

### ADR-NHN-001: Namespace prefix must match include path
- Decision: Every type declared in `include/mcp/**` will be declared in the namespace derived from its directory path, using `mcp` as the root and mapping each directory segment to a namespace segment.
- Rationale: The SRS requires directory-driven discovery and requires enforcement in CI.

### ADR-NHN-002: Umbrella headers will exist only as `all.hpp`
- Decision: Umbrella headers will exist only at `include/mcp/<module>/all.hpp` and `include/mcp/all.hpp`.
- Rationale: The SRS requires predictable entry points and forbids umbrella headers at other paths.

### ADR-NHN-003: Canonical type headers will declare canonical types
- Decision: Canonical type headers listed in the SRS will declare their canonical type and will not be umbrella headers.
- Rationale: The SRS requires per-type headers to be canonical include paths.

### ADR-NHN-004: Top-level API facade will expose only required aliases
- Decision: The top-level `namespace mcp` API surface will include dedicated facade headers that declare `mcp::Client`, `mcp::Server`, and `mcp::Session` as aliases to their module types.
- Rationale: The SRS requires a minimal top-level API facade and requires dedicated facade headers.

### ADR-NHN-005: Deterministic enforcement will run from `tools/checks/`
- Decision: A deterministic check script will validate namespace-to-path conformance for `include/mcp/**/*.hpp` and will be executed by CI via `tools/checks/run_checks.py`.
- Rationale: CI already executes deterministic checks from `tools/checks/run_checks.py`, and the SRS requires enforcement.

## Scope Guardrails (Out of Scope For This Plan)

- Protocol semantics, transport semantics, authorization semantics, and validation semantics are out of scope.
- Wire formats, HTTP headers, default values, timeouts, and error codes are out of scope.
- New externally visible features are out of scope.

## Target Files (Planned Touch List)

### Public headers and include surface
- `include/mcp/**/*.hpp`
- New facades:
  - `include/mcp/client.hpp`
  - `include/mcp/server.hpp`
  - `include/mcp/session.hpp`
- New umbrellas:
  - `include/mcp/all.hpp`
  - `include/mcp/client/all.hpp`
  - `include/mcp/server/all.hpp`
  - `include/mcp/jsonrpc/all.hpp`
  - `include/mcp/lifecycle/all.hpp`
  - Additional module umbrellas under `include/mcp/<module>/all.hpp` for every existing module directory

### Production sources
- `src/**/*.cpp`

### Tests and examples
- `tests/**/*.cpp`
- `examples/**/*.cpp`

### Tooling and CI enforcement
- `tools/checks/run_checks.py`
- New check script under `tools/checks/` for namespace layout enforcement
- `.github/workflows/ci.yml` will remain unchanged unless CI requires explicit wiring beyond `tools/checks/run_checks.py`

### Documentation
- `docs/api_overview.md`
- `docs/quickstart_client.md`
- `docs/quickstart_server.md`
- `docs/version_policy.md`

## Verification Strategy

- Deterministic enforcement checks must pass:
  - `python3 tools/checks/run_checks.py`
- Build must succeed:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release --parallel "${JOBS:-1}"`
- Tests must pass:
  - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`
- Formatting must pass:
  - `cmake --build build/vcpkg-unix-release --target clang-format-check`

## Risks / Unknowns

- Large-scale header moves and namespace moves will create widespread compile errors until the refactor is complete.
- Directory relocations will require coordinated include path updates across `include/`, `src/`, `tests/`, and `examples/`.
- The namespace enforcement check is required to avoid false positives caused by preprocessor conditionals and nested scopes.
- Windows builds will require path normalization in the enforcement tooling.
