# Task ID: [task-016]
# Task Name: [Resources (list/read/templates/subscribe + notifications)]

## Context
Implement server-side resource listing/reading, templates listing, and optional subscription updates.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Server Features: Resources)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/resources.md`

## Output / Definition of Done
* `include/mcp/server/resources.hpp` defines resource registration
* Implements:
  - `resources/list` (paginated)
  - `resources/read` (text or base64 blob)
  - `resources/templates/list` (paginated; RFC6570 template strings as opaque)
  - `resources/subscribe`/`resources/unsubscribe` when enabled
  - `notifications/resources/updated` and `notifications/resources/list_changed` when enabled
* Uses `-32002` for resource-not-found errors

## Step-by-Step Instructions
1. Define resource registry model (uri, metadata, content provider).
2. Implement list/read/templates endpoints with cursor pagination.
3. Implement subscription tracking and updated notifications.
4. Add tests for missing resources and capability-gated subscription behavior.

## Verification
* `ctest --test-dir build`
