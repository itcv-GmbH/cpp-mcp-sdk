# Namespace And Header Normalization (Breaking Refactor)

## Background

The repository is under active development and is not depended on by external projects.

The public include tree currently contains multiple organization patterns that increase cognitive load:

- Types under `include/mcp/client/` and `include/mcp/server/` are declared in `namespace mcp` rather than module namespaces.
- Several modules use a `detail` namespace or `detail/` directory inconsistently.
- Some headers are named for roles such as `*_class.hpp` while other types follow `snake_case` per-type header naming.
- Some internal helper functions exist as free functions in headers under `include/mcp/detail/` and additionally hide helpers in `namespace mcp::detail::detail`, which is redundant.

This refactor is required to make namespaces, directories, and filenames predictable and self-explanatory.

## User Stories

- As an SDK maintainer, I want namespaces to mirror the include directory layout so that I locate declarations quickly.
- As an SDK contributor, I want a single consistent pattern for public, detail, and internal headers so that new code follows a predictable structure.
- As a developer integrating this SDK, I want a clear and minimal top-level API surface so that user-facing entry points are obvious.

## Functional Requirements

### 1. Namespace Layout Policy

- Each type declared in `include/mcp/<module>/...` must be declared in `namespace mcp::<module>`.
- Each type declared in `include/mcp/<module>/<submodule>/...` must be declared in `namespace mcp::<module>::<submodule>`.
- `include/mcp/client/` declarations must live in `namespace mcp::client`.
- `include/mcp/server/` declarations must live in `namespace mcp::server`.
- `include/mcp/lifecycle/` declarations must live in `namespace mcp::lifecycle`.
- `include/mcp/jsonrpc/` declarations must live in `namespace mcp::jsonrpc`.
- `include/mcp/transport/` declarations must live in `namespace mcp::transport` and deeper transport namespaces.
- `include/mcp/auth/` declarations must live in `namespace mcp::auth`.
- `include/mcp/security/` declarations must live in `namespace mcp::security`.
- `include/mcp/http/` declarations must live in `namespace mcp::http` and deeper HTTP namespaces.
- `include/mcp/schema/` declarations must live in `namespace mcp::schema`.

### 2. Top-Level API Facade

- The SDK must expose a minimal top-level API surface in `namespace mcp`.
- The SDK must define the following top-level aliases:
  - `mcp::Client` must be an alias of `mcp::client::Client`.
  - `mcp::Server` must be an alias of `mcp::server::Server`.
  - `mcp::Session` must be an alias of `mcp::lifecycle::Session`.

- The canonical declarations for the alias targets are required to exist at the following header paths:
  - `include/mcp/client/client.hpp` is required to declare `mcp::client::Client`.
  - `include/mcp/server/server.hpp` is required to declare `mcp::server::Server`.
  - `include/mcp/lifecycle/session.hpp` is required to declare `mcp::lifecycle::Session`.

- The top-level aliases are required to be declared in dedicated facade headers:
  - `include/mcp/client.hpp` is required to include `<mcp/client/client.hpp>` and declare `mcp::Client`.
  - `include/mcp/server.hpp` is required to include `<mcp/server/server.hpp>` and declare `mcp::Server`.
  - `include/mcp/session.hpp` is required to include `<mcp/lifecycle/session.hpp>` and declare `mcp::Session`.

### 3. Header Naming Policy

- Each public type declared with the `class` keyword or the `struct` keyword must be declared in exactly one header.
- The header basename that declares a public type must match the type name in `snake_case`.
- Headers named `*_class.hpp` must not exist.

- The following files are required to exist as canonical type headers and are required to not be umbrella headers:
  - `include/mcp/client/client.hpp`
  - `include/mcp/server/server.hpp`
  - `include/mcp/lifecycle/session.hpp`
  - `include/mcp/jsonrpc/router.hpp`
  - `include/mcp/schema/validator.hpp`

- Any existing umbrella header at a canonical type header path is required to be removed or relocated to `all.hpp`.

### 4. Umbrella Headers

- A per-type header path is required to be the canonical include path for its type.
- Umbrella headers must exist only at the following paths:
  - `include/mcp/<module>/all.hpp`
  - `include/mcp/all.hpp`
- An umbrella header must contain only `#include` directives and must contain zero `class` declarations and zero `struct` declarations.

- The following module umbrellas are required to exist:
  - `include/mcp/client/all.hpp`
  - `include/mcp/server/all.hpp`
  - `include/mcp/jsonrpc/all.hpp`
  - `include/mcp/lifecycle/all.hpp`

### 5. Detail Headers And Namespaces

- A `detail/` directory under `include/mcp/<module>/detail/` must map to `namespace mcp::<module>::detail`.
- A file located in `include/mcp/detail/` must map to `namespace mcp::detail`.
- `namespace mcp::detail::detail` must not exist.
- Internal helper symbols that are not part of a module API must be declared in `namespace mcp::<module>::detail` or in `namespace mcp::detail`.

### 6. Free-Standing Function Policy

- Free-standing functions are required to exist for stateless algorithms and formatting/parsing utilities.
- A free-standing function must live in the narrowest module namespace that owns its semantics.
- A free-standing function that is not required to be in a header must be implemented in a `.cpp` file.

### 7. Directory And File Relocations

- Public headers must continue to live under `include/mcp/`.
- Domain-specific internal headers must live under `include/mcp/<module>/detail/`.
- Cross-domain internal headers must live under `include/mcp/detail/`.

### 8. Documentation Updates

- `docs/api_overview.md` is required to be updated to reflect the canonical namespaces and header paths defined by this specification.

## Non-Functional Requirements

### Quality Gates

- The repository must build successfully using `cmake --preset vcpkg-unix-release`.
- All tests must pass using `ctest --test-dir build/vcpkg-unix-release --output-on-failure`.
- The repository must pass `clang-format-check`.

### Enforcement

- The repository must include a deterministic automated check that fails when any `include/mcp/**` header declares types in a namespace that does not match the directory path rules in this specification.
- CI is required to execute this enforcement check.

### Compatibility

- Backward compatibility for historical include paths is not required.
- Backward compatibility for historical namespaces is not required.

## Out Of Scope

- Protocol semantics, transport semantics, authorization semantics, and validation semantics are not included.
- Wire formats, HTTP headers, default values, timeouts, and error codes are not included.
- New externally visible features are not included.
