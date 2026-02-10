# Task ID: [task-009]
# Task Name: [Implement Subprocess stdio Client (Cross-Platform)]

## Context
Provide a safe, cross-platform subprocess abstraction to spawn a stdio MCP server and communicate over stdin/stdout.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (stdio MUST provide spawn client)
* Platform constraints in `.docs/requirements/cpp-mcp-sdk.md` (Windows + POSIX)

## Output / Definition of Done
* `include/mcp/transport/stdio.hpp` exposes a subprocess-spawn API
* Implementation uses a cross-platform backend (e.g., Boost.Process)
* Shutdown sequence adheres to SRS guidance (close stdin; wait; SIGTERM; SIGKILL where applicable)

## Step-by-Step Instructions
1. Define a subprocess configuration struct (argv, env overrides, cwd).
2. Implement spawn:
   - connect pipes for stdin/stdout/stderr
   - expose stderr handling (capture/forward/ignore)
3. Implement shutdown semantics:
   - close stdin
   - wait with timeout
   - terminate escalation (POSIX signals; Windows terminate process)
4. Add integration-style test that spawns a minimal example server.

## Verification
* `ctest --test-dir build`
