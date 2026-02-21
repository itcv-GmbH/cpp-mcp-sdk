# Task ID: task-008
# Task Name: Add Deterministic Concurrency And Exception Tests

## Context

Threading and exception handling requirements must be enforced by deterministic tests to prevent regressions.

## Inputs

* `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md`
* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`
* Updated modules from `task-005`, `task-006`, and `task-007`
* `tests/`

## Output / Definition of Done

* New test cases will exist under `tests/` that validate:
  - concurrent request routing correctness
  - background thread exception containment
  - idempotent start-stop behavior
  - destructor no-throw behavior
* The test suite will run within bounded time under `ctest`.

## Step-by-Step Instructions

1. Add a new test file under `tests/` that exercises concurrent request routing using multiple threads.
2. Add a new test that triggers a controlled exception inside a background loop and asserts the error callback recorded the event.
3. Add a new test that performs repeated start-stop cycles for HTTP runtime and stdio subprocess transport.
4. Add a new test that destroys client and server runner instances while work is in flight and asserts the process remains alive and tests complete.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -V`
