# C++ MCP SDK Codebase Cleanup (Non-Functional Refactor)

## Background

This repository must have a predictable, navigable structure that reduces cognitive load for maintainers and contributors.

This effort must remove structural inconsistency and repository hygiene issues (file layout, header organization, and tracked artifacts) and must not change externally observable SDK behavior.

## User Stories

- As an SDK maintainer, I want each major public class to live in a dedicated header/source file pair so that code navigation and review become faster.
- As an SDK contributor, I want a documented and enforced module layout so that new code follows a consistent structure.
- As an SDK consumer, I want existing include paths to remain stable so that updates do not force mechanical include rewrites.
- As a release engineer, I want the repository to exclude generated artifacts so that diffs and packaging remain deterministic.

## Functional Requirements

### Scope and Constraints

- The refactor must not change protocol semantics, transport semantics, authorization semantics, or validation semantics.
- The refactor must not change public API meaning.
- The refactor must not change wire formats, HTTP headers, default values, timeouts, or error codes.
- The refactor must not add new externally visible features.

### Repository Hygiene

- The repository must not track build output directories.
- The repository must not track Python virtual environments.
- The repository must not track Python bytecode caches.
- The repository must not track editor, IDE, or OS metadata.

The following paths are required to be absent from the git index:

- `build/`
- `tests/integration/.venv/`
- `**/__pycache__/`
- `**/*.pyc`

### Directory Layout and File Placement

- Public headers must live under `include/mcp/`.
- Private implementation files must live under `src/`.
- Tests must live under `tests/`.
- Examples must live under `examples/`.

- Implementation source files must be placed in subdirectories that match their public module domains.
- The `src/` root must not contain domain-prefixed implementation files.

The following files are required to be relocated into a module directory:

- `src/auth_client_registration.cpp` must move to `src/auth/client_registration.cpp`.
- `src/auth_loopback_receiver.cpp` must move to `src/auth/loopback_receiver.cpp`.
- `src/auth_oauth_client.cpp` must move to `src/auth/oauth_client.cpp`.
- `src/auth_oauth_client_disabled.cpp` must move to `src/auth/oauth_client_disabled.cpp`.
- `src/auth_protected_resource_metadata.cpp` must move to `src/auth/protected_resource_metadata.cpp`.

### Include Policy

- Source and test code must not include project headers via relative paths that traverse directories.
- Source and test code must include public headers via `<mcp/...>` includes.

- Tests must not include headers from `src/`.

The internal threading boundary header currently located at `src/detail/thread_boundary.hpp` is required to be relocated into the public include tree under `include/mcp/detail/` and is required to be included via `<mcp/detail/...>` from all call sites.

### Public Header Organization Rules

- Each public header file under `include/mcp/` must define at most one top-level type declared with the `class` keyword or the `struct` keyword.
- Each public type declared with the `class` keyword or the `struct` keyword must have a corresponding header file whose basename matches the type name in `snake_case`.

- Existing umbrella headers are required to remain available at their current include paths.
- Umbrella headers are required to contain no `class` declarations and no `struct` declarations and are required to re-export types only by including the per-type headers.

### Required Header Splits (Concrete Targets)

The one-type-per-header rule is required to apply to all public headers under `include/mcp/`.

The following public headers are required to be split to satisfy the one-type-per-header rule while preserving existing include paths via umbrella headers:

- `include/mcp/transport/http.hpp` must become an umbrella header and must re-export:
  - `mcp::transport::HttpServerRuntime`
  - `mcp::transport::HttpClientRuntime`
  - `mcp::transport::http::StreamableHttpServer`
  - `mcp::transport::http::StreamableHttpClient`
  - `mcp::transport::http::SharedHeaderState`
  - `mcp::transport::http::SessionHeaderState`
  - `mcp::transport::http::ProtocolVersionHeaderState`

- `include/mcp/util/tasks.hpp` must become an umbrella header and must re-export:
  - `mcp::util::TaskStore`
  - `mcp::util::InMemoryTaskStore`
  - `mcp::util::TaskReceiver`

- `include/mcp/auth/oauth_client.hpp` must become an umbrella header and must re-export:
  - `mcp::auth::OAuthClientError`
  - `mcp::auth::OAuthTokenStorage`
  - `mcp::auth::InMemoryOAuthTokenStorage`

- `include/mcp/auth/client_registration.hpp` must become an umbrella header and must re-export:
  - `mcp::auth::ClientRegistrationError`
  - `mcp::auth::ClientCredentialsStore`
  - `mcp::auth::InMemoryClientCredentialsStore`

- `include/mcp/auth/loopback_receiver.hpp` must become an umbrella header and must re-export:
  - `mcp::auth::LoopbackReceiverError`
  - `mcp::auth::LoopbackRedirectReceiver`

Each split listed above is required to introduce per-type headers under the corresponding module directory. Each per-type header is required to contain the type declaration and any directly associated nested types.

### Build System Updates

- The CMake target `mcp_sdk` must continue to build after the file relocations and header splits.
- `CMakeLists.txt` is required to be updated so that `MCP_SDK_SOURCES` references the new `src/` paths.

### Enforcement

- The repository must include a deterministic, automated check that fails when any file under `include/mcp/` defines more than one top-level type declared with the `class` keyword or the `struct` keyword.
- The repository must include a deterministic, automated check that fails when any file under `src/` or `tests/` includes a project header via a relative include that traverses directories.
- The CI workflow is required to execute the enforcement checks.

## Non-Functional Requirements

### Compatibility

- Existing include paths used by SDK consumers must remain valid.
- The SDK must remain source-compatible for existing consumers that include the current umbrella headers.

### Quality Gates

- All existing unit tests and conformance tests must pass.
- The repository must pass `clang-format-check`.
- The repository must build on the CI platforms defined in `.github/workflows/ci.yml`.

### Refactor Safety

- The refactor must be limited to code movement, header factoring, include updates, and removal of tracked artifacts.
- Public behavior must remain unchanged as verified by the existing test suite.
