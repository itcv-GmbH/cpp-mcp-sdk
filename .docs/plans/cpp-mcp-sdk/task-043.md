# Task ID: [task-043]
# Task Name: [vcpkg Port Readiness (Overlay Port + Consumer Verification)]

## Context
Prepare this SDK to be published to the upstream vcpkg registry later by validating the packaging surface early. This task creates and validates a local overlay port so the team can iterate on vcpkg-port compatibility before upstreaming.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Dependencies and Packaging)
* `.docs/plans/cpp-mcp-sdk/task-039.md` (CMake install/export packaging)
* vcpkg overlay ports requirements (a port must contain `portfile.cmake` and `vcpkg.json`)

## Output / Definition of Done
* `vcpkg/ports/mcp-cpp-sdk/vcpkg.json` exists
* `vcpkg/ports/mcp-cpp-sdk/portfile.cmake` exists and builds/installs the SDK via CMake
* `vcpkg-configuration.json` includes `./vcpkg/ports` in `overlay-ports` (or CI passes `--overlay-ports`)
* A clean consumer project can install and link via vcpkg + `find_package()`

## Step-by-Step Instructions
1. Create an overlay port directory `vcpkg/ports/mcp-cpp-sdk/` with:
   - `vcpkg.json` describing the port metadata (name, version, license) and dependencies.
   - `portfile.cmake` that:
     - fetches the SDK source (for local dev, `SOURCE_PATH` may point at the repo)
     - configures with vcpkg toolchain using vcpkg CMake helpers
     - builds and installs
     - runs `vcpkg_cmake_config_fixup()` (or equivalent) so the config package lands under `share/<port>/`.
2. Ensure the SDK’s CMake install exports do not embed absolute build paths.
3. Add a minimal consumer verification project:
   - depends on the vcpkg-installed SDK
   - uses `find_package()` and links to exported targets
4. Document the “how to upstream” checklist for later:
   - stable versioning and tags
   - license file and vcpkg copyright file mapping
   - CI evidence and reproducible builds

## Verification
* `vcpkg install mcp-cpp-sdk --overlay-ports=./vcpkg/ports`
* `cmake -S examples/consumer_find_package -B build-consumer -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"`
* `cmake --build build-consumer`
