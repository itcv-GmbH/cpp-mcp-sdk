# Task ID: [task-023]
# Task Name: [Client: Elicitation (form+url, URL safety, completion notifications)]

## Context
Support server-initiated elicitation, including form-mode schema restrictions and URL-mode safety invariants.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Client Features: Elicitation)
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/elicitation.md`

## Output / Definition of Done
* `include/mcp/client/elicitation.hpp` defines elicitation handler interface
* Implements `elicitation/create` handling with:
  - form mode restricted schema enforcement
  - url mode safety requirements (no auto-open; explicit consent; display full URL/domain)
* Supports `URLElicitationRequiredError` (`-32042`) per schema
* Handles `notifications/elicitation/complete` for URL-mode completion

## Step-by-Step Instructions
1. Define elicitation handler callback(s) for form and URL modes.
2. Implement inbound `elicitation/create` handler.
3. Enforce form schema restriction (flat object with primitive properties).
4. Add URL policy helper API so host can render consent UI safely.
5. Implement support for `notifications/elicitation/complete` and ignore unknown IDs.
6. Add tests for mode gating and action model (accept/decline/cancel).

## Verification
* `ctest --test-dir build`
