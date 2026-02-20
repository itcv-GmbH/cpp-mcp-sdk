# Task ID: task-010
# Task Name: Wire New Sources/Tests into CMake

## Context
New runner sources, tests, and example targets must be built in CI and locally.

## Inputs
* `CMakeLists.txt`
* `tests/CMakeLists.txt`
* Existing example build patterns
* New files from tasks 003-009 and 012-013

## Output / Definition of Done
* Runner sources added to the main SDK library target.
* Updated transport sources (session issuance) are built in CI.
* New tests added and registered in `ctest`.
* New example target added for dual-transport example.

## Step-by-Step Instructions
1. Add runner sources to the SDK library:
   - `src/server/stdio_runner.cpp`
   - `src/server/streamable_http_runner.cpp`
   - `src/server/combined_runner.cpp`
2. Add test executables:
   - `mcp_sdk_test_server_stdio_runner`
   - `mcp_sdk_test_server_streamable_http_runner`
   - `mcp_sdk_test_server_combined_runner`
3. Register them via `add_test(...)` with stable names:
   - `mcp_sdk_server_stdio_runner_test`
   - `mcp_sdk_server_streamable_http_runner_test`
   - `mcp_sdk_server_combined_runner_test`
4. Add example executable `mcp_sdk_example_dual_transport_server` under the existing examples structure.
5. Ensure conformance test changes from `task-015` are compiled and registered (extend existing `mcp_sdk_test_conformance_streamable_http_transport` target as needed).

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_TESTS=ON -DMCP_SDK_BUILD_EXAMPLES=ON && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
