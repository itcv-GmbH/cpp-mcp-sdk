# Task ID: task-005
# Task Name: Update build inputs and verify quality gates

## Context

File relocation and include updates are required to be reflected in the build system.

## Inputs

- Output of `task-004`
- `CMakeLists.txt`
- SRS: `.docs/requirements/cpp-source-namespace-and-include-normalization.md` (Non-Functional Requirements)

## Output / Definition of Done

- The repository is required to build successfully using `cmake --preset vcpkg-unix-release`.
- All tests are required to pass using `ctest --test-dir build/vcpkg-unix-release --output-on-failure`.
- The repository is required to pass `clang-format-check`.
