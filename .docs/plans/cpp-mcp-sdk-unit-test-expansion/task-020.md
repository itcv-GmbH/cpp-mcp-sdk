# Task ID: [task-020]
# Task Name: [Expand Unit Tests: Client Facade]

## Context
Broaden client facade tests around capability gating, typed wrappers, and inbound client-feature handlers (roots/sampling/elicitation) with more negative cases.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Client features: Roots/Sampling/Elicitation; capability gating)
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/roots.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/sampling.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/elicitation.md`
* `include/mcp/client/client.hpp`
* `tests/client_test.cpp`

## Output / Definition of Done
* `tests/client_test.cpp` adds tests for:
  - inbound `roots/list` returns correct errors when roots capability absent
  - sampling role constraints and tool-use/tool-result balancing errors
  - elicitation URL-mode safety invariants (no auto-open; required error `-32042` paths)
  - pagination helper behavior for list endpoints (cursor handling)

## Step-by-Step Instructions
1. Add new sections to existing client fixture tests (reuse the in-process server fixture where possible).
2. Add negative tests for sampling with invalid roles and mismatched tool-use/tool-result.
3. Add elicitation URL-mode tests ensuring required error and notification handling paths.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_client_test --output-on-failure`
