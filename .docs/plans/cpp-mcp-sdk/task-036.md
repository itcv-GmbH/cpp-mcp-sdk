# Task ID: [task-036]
# Task Name: [Examples (server/client; bidirectional sampling/elicitation/tasks)]

## Context
Provide runnable examples demonstrating required features and recommended usage patterns.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Documentation; Acceptance Criteria)

## Output / Definition of Done
* `examples/stdio_server/` demonstrates tools/resources/prompts over stdio
* `examples/http_server_auth/` demonstrates Streamable HTTP + HTTPS + authorization
* `examples/http_client_auth/` demonstrates OAuth discovery + loopback receiver
* `examples/bidirectional_sampling_elicitation/` demonstrates server-initiated requests
* At least one example demonstrates tasks (`params.task` + tasks/result)

## Step-by-Step Instructions
1. Implement minimal but complete example programs with clear build/run instructions.
2. Ensure examples use only public SDK APIs.
3. Add scripts/config for local TLS test certs.
4. Add README files per example.

## Verification
* `cmake -S . -B build -DMCP_SDK_BUILD_EXAMPLES=ON`
* `cmake --build build`
