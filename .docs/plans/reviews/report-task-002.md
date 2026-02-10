# Review Report: task-002 (/ Select Dependencies (vcpkg ports; JSON + JSON Schema 2020-12))

## Status
**FAIL**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [ ] Implementation matches `task-002.md` instructions.
- [ ] Definition of Done met.
- [x] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `vcpkg install`
*   **Result:** Pass (installs core deps; emits baseline warning due to duplicate pinning sources).

*   **Command Run:** `cmake -S . -B build-task002-verify -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"`
*   **Result:** Fail (fresh build directory fails: `Could not find a package configuration file provided by "Catch2"`).

*   **Command Run:** `cmake --build build-task002-verify`
*   **Result:** Not run (configure failed).

## Issues Found (If FAIL)
*   **Critical:** Task verification commands do not succeed on a clean configure/build. `cmake/Dependencies.cmake` requires `find_package(Catch2 CONFIG REQUIRED)` whenever `MCP_SDK_BUILD_TESTS=ON`, but `MCP_SDK_BUILD_TESTS` defaults to ON in `cmake/Options.cmake` while Catch2 is not installed by default from `vcpkg.json` (it is behind the `tests` feature). This breaks `cmake -S . -B build ...` after a plain `vcpkg install`.
*   **Major:** Pinning strategy/documentation is internally inconsistent and produces a vcpkg warning. `vcpkg-configuration.json` overrides the default registry (kind `git` + `baseline`), so `builtin-baseline` in `vcpkg.json` is ignored; `.docs/plans/cpp-mcp-sdk/master_plan.md` claims `builtin-baseline` pins the registry commit and that `vcpkg-configuration.json` “mirrors” it.
*   **Minor:** Dependency docs/notes are slightly inaccurate:
    - `cmake/Dependencies.cmake` comment claims `jsoncons::json` target; vcpkg’s generated usage indicates target `jsoncons`.
    - `docs/dependencies.md` states `MCP_SDK_BUILD_TESTS` default is ON while also implying Catch2 is optional via vcpkg feature; these are in tension with the task verification steps.

## Required Actions
1. Make the task verification steps succeed from a clean build directory:
   - Either change defaults so `MCP_SDK_BUILD_TESTS` does not require Catch2 unless explicitly enabled (recommended: default tests OFF, or split “smoke tests” vs “Catch2 tests”), OR move Catch2 into the default vcpkg dependencies (if tests are meant to be ON by default).
2. Pick a single source of truth for vcpkg baseline pinning (either `builtin-baseline` in `vcpkg.json` with no overriding default-registry, or baseline only in `vcpkg-configuration.json`) and update `.docs/plans/cpp-mcp-sdk/master_plan.md` and `docs/dependencies.md` accordingly to remove the warning and reflect actual behavior.
3. Fix the small documentation inaccuracies around CMake targets (`jsoncons`) and test enablement guidance so docs match the build.
