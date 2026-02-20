# Task ID: task-011
# Task Name: Full Conformance + Regression Run

## Context
Runners must not change protocol behavior. Final gate is a full build and full test run including conformance.

## Inputs
* All changes from `task-001` through `task-010`
* Existing conformance suite in `tests/conformance/`

## Output / Definition of Done
* All unit tests pass.
* All conformance tests pass.
* Examples build successfully.

## Step-by-Step Instructions
1. Configure and build:
   - `cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_TESTS=ON -DMCP_SDK_BUILD_EXAMPLES=ON`
   - `cmake --build build/vcpkg-unix-release`
2. Run all tests:
   - `ctest --test-dir build/vcpkg-unix-release --output-on-failure`
3. Run key conformance tests explicitly (sanity):
   - `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_stdio_transport_test --output-on-failure`
   - `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_streamable_http_transport_test --output-on-failure`

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_TESTS=ON -DMCP_SDK_BUILD_EXAMPLES=ON && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
