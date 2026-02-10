# Task ID: [task-017]
# Task Name: [Prompts (list/get + list_changed)]

## Context
Implement server-side prompts listing and retrieval, including argument handling and list-change notifications.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Server Features: Prompts)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/prompts.md`

## Output / Definition of Done
* `include/mcp/server/prompts.hpp` defines prompt registration
* `prompts/list` supports pagination
* `prompts/get` accepts `arguments` and returns `messages` per schema
* `notifications/prompts/list_changed` emitted when enabled

## Step-by-Step Instructions
1. Define prompt registry model (name, metadata, argument schema, handler).
2. Implement list/get endpoints and validate argument names/required semantics.
3. Add list-changed notification support.
4. Add tests for argument validation and pagination.

## Verification
* `ctest --test-dir build`
