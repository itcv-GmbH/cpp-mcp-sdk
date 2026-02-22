# Task ID: task-033
# Task Name: Full Build, Test, and Formatting Gate

## Context
This task is responsible for executing the required quality gates after the refactor is complete.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Quality Gates)
*   `.github/workflows/ci.yml` (CI platform set)

## Dependencies
*   This task depends on: `task-032`.

## Output / Definition of Done
*   All unit tests and conformance tests pass.
*   `clang-format-check` passes.
*   The repository builds successfully using the CI presets.

## Step-by-Step Instructions
1.  Configure the project using the CI-aligned preset.
2.  Build the project.
3.  Run the full test suite.
4.  Run `clang-format-check`.

## Verification
*   `cmake --preset vcpkg-unix-release`
*   `cmake --build build/vcpkg-unix-release`
*   `ctest --test-dir build/vcpkg-unix-release --output-on-failure`
*   `cmake --build build/vcpkg-unix-release --target clang-format-check`
