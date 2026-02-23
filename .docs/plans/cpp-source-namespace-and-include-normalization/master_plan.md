# C++ Source Namespace And Include Normalization (Non-Functional Refactor)

This plan is responsible for normalizing `src/**/*.cpp` so that:
- MCP namespaces opened in `.cpp` files match directory layout under `src/`
- repository header includes in `.cpp` files use a consistent quoting policy
- `.cpp` files do not include umbrella headers (`mcp/all.hpp`, `mcp/<module>/all.hpp`)

## Inputs

- SRS: `.docs/requirements/cpp-source-namespace-and-include-normalization.md`
- Source tree: `src/`
- Build system: `CMakeLists.txt`

## Scope Guardrails

- This plan must not normalize `tests/` translation units.
- This plan must not normalize `examples/` translation units.
- This plan must not change externally observable SDK behavior.
- This plan must not add an automated enforcement check for `src/` namespace layout.

## Verification Strategy

- Build must succeed:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release --parallel "${JOBS:-1}"`
- Tests must pass:
  - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`
- Formatting must pass:
  - `cmake --build build/vcpkg-unix-release --target clang-format-check`
