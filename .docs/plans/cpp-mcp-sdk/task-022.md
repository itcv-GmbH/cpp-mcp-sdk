# Task ID: [task-022]
# Task Name: [Client: Sampling (handle sampling/createMessage constraints)]

## Context
Support server-initiated sampling requests and enforce schema and semantic constraints (roles, tool-use constraints, capability gating).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Client Features: Sampling)
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/sampling.md`

## Output / Definition of Done
* `include/mcp/client/sampling.hpp` defines sampling handler interface
* Enforces:
  - roles only `user`/`assistant`
  - tool fields only when `sampling.tools` declared
  - tool_use/tool_result balancing constraints
* Returns `-1` when user rejects sampling request (client policy)

## Step-by-Step Instructions
1. Define sampling handler callback that host app implements.
2. Implement inbound `sampling/createMessage` handler.
3. Validate semantics before invoking host callback.
4. Add tests for invalid roles and tool capability violations.

## Verification
* `ctest --test-dir build`
