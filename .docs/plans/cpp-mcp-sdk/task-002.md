# Task ID: [task-002]
# Task Name: [Select Dependencies (vcpkg ports; JSON + JSON Schema 2020-12)]

## Context
Pick and lock the foundational libraries that determine feasibility: JSON representation, JSON Schema 2020-12 validation, HTTP/TLS primitives.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Schema Validation; HTTPS; dependency minimization)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.json` (JSON Schema 2020-12)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts` (authoritative surface)

## Output / Definition of Done
* ADR update in `.docs/plans/cpp-mcp-sdk/master_plan.md` reflects final choices
* `vcpkg.json` lists concrete vcpkg port dependencies and features (tests/examples behind features or CMake options)
* `vcpkg-configuration.json` pins registry baseline (or `builtin-baseline` is set in `vcpkg.json`)
* `cmake/Dependencies.cmake` updated to rely on `find_package()` targets provided by vcpkg
* A short dependency policy document added (e.g. `docs/dependencies.md`) describing required/optional deps

## Step-by-Step Instructions
1. Confirm dependency manager: vcpkg manifest mode for all third-party dependencies.
2. Select JSON + JSON Schema stack that is actually JSON Schema 2020-12 capable:
   - Choose `jsoncons` for JSON + JSON Schema (explicit draft 2020-12 support).
   - Do not choose draft-7-only validators (insufficient for MCP pinned schema 2020-12).
3. Select transport/auth stack ports:
   - `boost-asio`, `boost-beast`, `boost-process` (stdio subprocess)
   - `openssl`
   - test framework: `catch2`
4. Encode choices into `vcpkg.json`:
   - list dependencies by their vcpkg port names
   - add platform qualifiers if needed (e.g., `boost-process` does not support some platforms; this project targets desktop OSes)
   - optionally use manifest features for `tests` and `examples`
5. Decide version pinning strategy:
   - set `builtin-baseline` in `vcpkg.json` to a pinned vcpkg commit
   - keep dependency upgrades explicit (via baseline bumps)
6. Update `cmake/Dependencies.cmake` to use `find_package()` targets from vcpkg (avoid FetchContent).

## Verification
* `vcpkg install` (from repo root)
* `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"`
* `cmake --build build`
