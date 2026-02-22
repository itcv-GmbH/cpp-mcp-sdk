# Task ID: task-032
# Task Name: Wire Enforcement Checks Into CMake and CI

## Context
This task is responsible for ensuring enforcement checks execute in CI and are available as local build targets.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Enforcement)
*   `.github/workflows/ci.yml`
*   `CMakeLists.txt`
*   `tools/checks/run_checks.py`

## Output / Definition of Done
*   `CMakeLists.txt` defines a custom target that runs `python3 tools/checks/run_checks.py`.
*   `.github/workflows/ci.yml` runs the enforcement checks in CI and fails the workflow when checks fail.
*   CI executes enforcement checks in all jobs that build and test the SDK.

## Step-by-Step Instructions
1.  Add a `codebase-check` custom target to `CMakeLists.txt` that runs `python3 tools/checks/run_checks.py` from the repository root.
2.  Update `.github/workflows/ci.yml` to install a Python runtime and invoke `python3 tools/checks/run_checks.py` after checkout and before build steps.
3.  Add the enforcement step to the build-and-test matrix job, the feature-matrix job, and the integration-reference-tests job.
4.  Run the checks locally and verify the target and workflow commands succeed.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release --target codebase-check`
