# Task ID: task-001
# Task Name: Define Coverage Matrix And Gate

## Context
This task will define the authoritative integration-test coverage requirements for Python reference SDK interop. This task will produce a machine-checkable matrix that maps every required MCP 2025-11-25 request and notification name to at least one automated integration test.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Protocol Surface list)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts` (Authoritative method and notification names)
* `tests/integration/README.md` (Current integration test coverage)
* `tests/integration/CMakeLists.txt` (Current integration test wiring)

## Output / Definition of Done
* `tests/integration/COVERAGE.md` exists and enumerates every request and notification required by `.docs/requirements/cpp-mcp-sdk.md`.
* `tests/integration/COVERAGE.md` maps each item to at least one CTest test name.
* `tests/integration/scripts/reference_coverage_gate.py` exists and exits non-zero when any required item is not mapped.
* `tests/integration/CMakeLists.txt` registers a CTest named `mcp_sdk_integration_reference_coverage_gate` that runs the coverage gate.

## Step-by-Step Instructions
1. Create `tests/integration/COVERAGE.md` and copy the complete request and notification list from `.docs/requirements/cpp-mcp-sdk.md`.
2. Add a section in `tests/integration/COVERAGE.md` that lists every CTest integration_reference test name and the protocol surface items it covers.
3. Create `tests/integration/scripts/reference_coverage_gate.py` that parses `tests/integration/COVERAGE.md` and asserts that every required item is mapped.
4. Update `tests/integration/CMakeLists.txt` to register `mcp_sdk_integration_reference_coverage_gate` with label `integration_reference`.
5. Ensure the coverage gate runs after `mcp_sdk_integration_reference_setup_python_sdk` and before the other integration_reference tests.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_integration_reference_coverage_gate --output-on-failure`
