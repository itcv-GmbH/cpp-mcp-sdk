# Task ID: [task-012]
# Task Name: [Expand Unit Tests: Stdio Subprocess Client]

## Context
Increase subprocess-spawn test coverage to reduce platform-specific regressions (pipes, shutdown sequencing, stderr modes).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (stdio spawn client requirement)
* `include/mcp/transport/stdio.hpp`
* `tests/transport_stdio_subprocess_test.cpp`
* `tests/transport_stdio_subprocess_server.cpp`

## Output / Definition of Done
* `tests/transport_stdio_subprocess_test.cpp` adds tests for:
  - `stderrMode=kForward` behavior (does not crash; still exits cleanly)
  - spawn option validation (empty argv rejected; invalid executable path errors are actionable)
  - shutdown idempotency (`shutdown()` can be called twice)
  - `waitForExit` timeout behavior (returns false without throwing)

## Step-by-Step Instructions
1. Add negative spawn tests for invalid argv/path.
2. Add a test covering `stderrMode=kForward` and confirming subprocess still exchanges a ping.
3. Add idempotent shutdown test.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_stdio_subprocess_test --output-on-failure`
