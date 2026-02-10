# Task ID: [task-042]
# Task Name: [Legacy 2024-11-05 HTTP+SSE Server Compatibility (Optional)]

## Context
Optionally allow SDK servers to serve both Streamable HTTP and legacy HTTP+SSE endpoints to support older clients.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (SHOULD support older revision)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (Backwards Compatibility section)

## Output / Definition of Done
* Server can host legacy endpoints alongside Streamable HTTP endpoint
* Legacy transport feature is behind a build/runtime flag
* Docs describe how to enable and security implications

## Step-by-Step Instructions
1. Implement legacy HTTP+SSE endpoints and bridge to the same router/session core.
2. Ensure header/version handling does not conflict with Streamable HTTP.
3. Add tests verifying legacy clients can initialize and call tools.
4. Document that legacy support increases attack surface and should be disabled by default.

## Verification
* `ctest --test-dir build -R legacy_server`
