# Task ID: task-009
# Task Name: Update Quickstarts + API Overview

## Context
The SRS requires quickstarts for server stdio + Streamable HTTP + authorization. With runners, the quickstarts and API overview must point users at the runner-based path.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Documentation requirements)
* `docs/quickstart_server.md`
* `docs/api_overview.md`
* Updated examples from `task-008`

## Output / Definition of Done
* `docs/quickstart_server.md` updated to reference runner-based examples and include a short snippet showing runner usage.
* `docs/api_overview.md` updated to include runner utilities in module boundaries.

## Step-by-Step Instructions
1. Update quickstart narrative to highlight:
   - STDIO runner guarantees “no logs to stdout”
   - HTTP runner provides `start()/stop()` and creates one `mcp::Server` per `MCP-Session-Id` via `ServerFactory`
   - combined runner supports starting STDIO, HTTP, or both in one process
2. Ensure docs do not encourage the deprecated `StdioTransport` instance API.
3. Ensure the Streamable HTTP quickstart examples include:
   - `MCP-Session-Id` after initialize when the server issues it
   - `MCP-Protocol-Version` on requests after initialize (per spec)
4. Keep commands stable; only adjust if example target names change.

## Verification
* Re-run the commands in `docs/quickstart_server.md` locally.
