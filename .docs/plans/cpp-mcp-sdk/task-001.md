# Task ID: [task-001]
# Task Name: [Create Repo Layout + CMake + vcpkg Skeleton]

## Context
Establish the SDK’s repository structure and a deterministic, cross-platform CMake build that will be used by every subsequent task.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (NFR: Platform Support; Dependencies and Packaging)
* `.docs/requirements/mcp-spec-2025-11-25/MANIFEST.md` (pinned normative inputs)

## Output / Definition of Done
* Root `CMakeLists.txt` builds an (empty) SDK library target with C++17 enabled
* `cmake/Options.cmake` defines build options (shared/static, tests, examples, TLS)
* `tests/CMakeLists.txt` and `examples/CMakeLists.txt` exist and are wired behind options
* `vcpkg.json` exists and enables vcpkg manifest mode for the repo
* `vcpkg-configuration.json` exists (baseline/registry pinning; overlay ports hook)
* `cmake --build` works on Linux/macOS/Windows toolchains (no product code required yet)

## Step-by-Step Instructions
1. Create the baseline directory layout: `include/`, `src/`, `tests/`, `examples/`, `cmake/`, `third_party/` (if vendoring).
2. Add a root `CMakeLists.txt` with:
   - `project(mcp_cpp_sdk LANGUAGES CXX)`
   - `CMAKE_CXX_STANDARD 17` and `CMAKE_CXX_STANDARD_REQUIRED ON`
   - library target (name TBD) and include directories
   - options to enable/disable tests/examples
3. Add `cmake/Options.cmake` and `cmake/Dependencies.cmake` placeholders to centralize dependency wiring.
4. Add `vcpkg.json` at repo root:
   - include `"$schema"` pointing to vcpkg’s manifest schema
   - set `"builtin-baseline"` to a pinned vcpkg commit for deterministic dependency resolution
   - start with empty `"dependencies"` (filled in `task-002`)
5. Add `vcpkg-configuration.json` at repo root:
   - set `"$schema"` to vcpkg-configuration schema
   - optionally set `default-registry` to the official vcpkg git registry with a pinned `baseline`
   - include an `overlay-ports` entry pointing at `./vcpkg/ports` (used later for local port testing)
6. Add install/export scaffolding suitable for `find_package()` later (can be a stub initially).
7. Ensure Windows generator support (MSVC) and multi-config safety (`CMAKE_CONFIGURATION_TYPES`).
8. (Recommended) Add `CMakePresets.json` wiring vcpkg toolchain and common triplets.

## Verification
* `vcpkg install` (from repo root; manifest mode)
* `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"`
* `cmake --build build`
