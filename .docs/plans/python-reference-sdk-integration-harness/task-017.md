# Task ID: task-017
# Task Name: Add CI Job For Python Reference Integration Suite

## Context
This task will ensure the Python reference SDK integration tests run automatically in CI so interoperability regressions are blocked.

## Inputs
* `.github/workflows/ci.yml`
* `tests/integration/CMakeLists.txt`
* `.docs/requirements/cpp-mcp-sdk.md` (Interop requirement)

## Output / Definition of Done
* `.github/workflows/ci.yml` runs a job that:
  - configures with `-DMCP_SDK_INTEGRATION_TESTS=ON`
  - builds the project
  - runs `ctest -R integration_reference --output-on-failure`
* The job runs on at least macOS and Linux.
* The job publishes failing test output in CI logs.

## Step-by-Step Instructions
1. Add a new CI job or extend an existing job to install a Python 3 interpreter.
2. Configure CMake with `-DMCP_SDK_INTEGRATION_TESTS=ON` using the vcpkg preset.
3. Build and run the `integration_reference` labeled tests.
4. Ensure the job fails when any integration_reference test fails.

## Verification
* `gh workflow run` is required to be executed by the implementer to validate the CI job after merge.
