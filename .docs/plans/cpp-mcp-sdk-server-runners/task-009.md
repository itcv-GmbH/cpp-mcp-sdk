# Task ID: task-009
# Task Name: Update Quickstarts + API Overview

## Context
The SRS requires quickstarts for server stdio + Streamable HTTP + authorization. With runners, the quickstarts and API overview should point users at the new ergonomics-first path.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Documentation requirements)
* `docs/quickstart_server.md`
* `docs/api_overview.md`
* Updated examples from `task-008`

## Output / Definition of Done
* `docs/quickstart_server.md` updated to reference runner-based examples and (optionally) include a short snippet showing runner usage.
* `docs/api_overview.md` updated to include runner utilities in module boundaries.

## Step-by-Step Instructions
1. Update quickstart narrative to highlight:
   - STDIO runner guarantees “no logs to stdout”
   - HTTP runner provides `start()/stop()` and per-session factory model
   - combined runner supports starting STDIO, HTTP, or both in one process
2. Ensure docs do not encourage the deprecated `StdioTransport` instance API.
3. Keep commands stable; only adjust if example target names change.

## Verification
* Re-run the commands in `docs/quickstart_server.md` locally.
