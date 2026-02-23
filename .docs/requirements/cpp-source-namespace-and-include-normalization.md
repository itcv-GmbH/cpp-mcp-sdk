# C++ Source Namespace And Include Normalization (Non-Functional Refactor)

## Background

The public header tree under `include/mcp/` is normalized and enforced.

The C++ implementation files under `src/` are required to follow the same directory-driven namespace rules so that maintainers predict where definitions live and so that refactors remain mechanical.

This effort is required to reduce cognitive load and is required to not change externally observable SDK behavior.

## User Stories

- As an SDK maintainer, I want `src/` namespaces to mirror the `src/` directory layout so that I locate definitions quickly.
- As an SDK contributor, I want project include directives in `.cpp` files to use one consistent style so that diffs remain mechanical.
- As an SDK maintainer, I want `.cpp` files to avoid umbrella headers so that translation units include only what they use.

## Functional Requirements

### 1. Scope

- This specification must apply to implementation files under `src/**/*.cpp`.
- This specification must not apply to files under `tests/`.
- This specification must not apply to files under `examples/`.
- This specification must supersede any existing repository requirements that mandate angle-bracket includes for repository headers in `src/**/*.cpp`.

### 2. Source Namespace Layout Policy

- Each non-anonymous namespace opened in a `.cpp` file under `src/` that begins with `mcp` is required to match the namespace derived from its directory path.

- A `.cpp` file located at `src/<segment_1>/<segment_2>/.../<segment_n>/file.cpp` is required to treat the expected namespace as:
  - `namespace mcp::<segment_1>::<segment_2>::...::<segment_n>`
  - where `segment_i` are directory segments only and the filename is excluded.

- A `.cpp` file located under `src/detail/...` is required to declare MCP symbols only under `namespace mcp::detail` and deeper `mcp::detail::...` namespaces.

- A `.cpp` file located under `src/<module>/detail/...` is required to declare MCP symbols only under `namespace mcp::<module>::detail` and deeper `mcp::<module>::detail::...` namespaces.

- `namespace mcp::detail::detail` must not exist.
- `namespace mcp::<module>::detail::detail` must not exist.

- A `.cpp` file is required to be relocated within `src/` when it implements symbols in an MCP namespace that is not derived from its current directory path under this specification.

### 3. Project Include Style For `.cpp`

- Any `#include` directive in `src/**/*.cpp` that targets a repository header under `include/mcp/**` is required to use double quotes and an include path that begins with `mcp/`.
- Any `#include` directive in `src/**/*.cpp` that targets a repository header under `include/mcp/**` must not use angle brackets.

### 4. Umbrella Header Prohibition In `.cpp`

- A `.cpp` file under `src/` must not include `mcp/all.hpp`.
- A `.cpp` file under `src/` must not include any module umbrella header `mcp/<module>/all.hpp`.

### 5. Behavior Preservation

- This refactor must not change protocol semantics, transport semantics, authorization semantics, or validation semantics.
- This refactor must not change wire formats, HTTP headers, default values, timeouts, or error codes.
- This refactor must not add new externally visible features.

## Non-Functional Requirements

### Quality Gates

- The repository must build successfully using `cmake --preset vcpkg-unix-release`.
- All tests must pass using `ctest --test-dir build/vcpkg-unix-release --output-on-failure`.
- The repository must pass `clang-format-check`.

### Out Of Scope

- Include ordering within translation units is out of scope.
- Any enforcement tooling for `src/` namespace layout is out of scope.
- Normalization of `tests/` and `examples/` translation units is out of scope.
