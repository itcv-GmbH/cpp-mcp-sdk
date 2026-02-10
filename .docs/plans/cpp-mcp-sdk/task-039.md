# Task ID: [task-039]
# Task Name: [Packaging (CMake config package; static/shared options)]

## Context
Make the SDK consumable via `find_package()` and support static/shared builds.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Packaging requirements)

## Output / Definition of Done
* Install rules added for headers and library
* `mcp_sdkConfig.cmake` (or equivalent) generated/installed
* Options:
  - `BUILD_SHARED_LIBS`
  - enable/disable TLS/auth features
* A basic `find_package(mcp_sdk)` consumer example documented
* vcpkg readiness:
  - install layout is compatible with a vcpkg port
  - an overlay port exists under `vcpkg/ports/mcp-cpp-sdk/` for local verification

## Step-by-Step Instructions
1. Add export target and config package templates.
2. Ensure dependency propagation is correct (Boost/OpenSSL).
3. Add a small `examples/consumer_find_package/` or doc snippet.
4. Add a local overlay port for development verification:
   - create `vcpkg/ports/mcp-cpp-sdk/vcpkg.json` and `vcpkg/ports/mcp-cpp-sdk/portfile.cmake`
   - ensure the port uses vcpkg CMake helpers (`vcpkg-cmake`, `vcpkg-cmake-config` host deps)
   - ensure the port installs CMake config files so consumers can `find_package()` the SDK

## Verification
* `cmake --install build --prefix build/prefix`
* `cmake -S examples/consumer_find_package -B build-consumer -DCMAKE_PREFIX_PATH=build/prefix`
* `vcpkg install mcp-cpp-sdk --overlay-ports=./vcpkg/ports` (from repo root)
