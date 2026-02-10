# Task ID: [task-041]
# Task Name: [Legacy 2024-11-05 HTTP+SSE Client Fallback (Optional)]

## Context
Provide optional interoperability with legacy servers using the deprecated 2024-11-05 HTTP+SSE transport.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (SHOULD support older revision)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (Backwards Compatibility section)
* `.docs/requirements/mcp-spec-2025-11-25/spec/2024-11-05/basic/transports` (upstream reference; not mirrored)

## Output / Definition of Done
* Client implements fallback detection:
  - try POST initialize (new transport)
  - on 400/404/405, try legacy GET SSE and expect initial `endpoint` event
* Legacy transport feature is behind a build/runtime flag

## Step-by-Step Instructions
1. Add client transport detection logic per spec guidance.
2. Implement legacy endpoint model (`/rpc` + `/events` or server-provided endpoint from `endpoint` event).
3. Add conformance-style tests using a legacy test fixture.
4. Document limitations and defaults (off by default unless required).

## Verification
* `ctest --test-dir build -R legacy_client`
