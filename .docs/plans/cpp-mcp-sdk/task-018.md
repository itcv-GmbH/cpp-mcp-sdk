# Task ID: [task-018]
# Task Name: [Server Utilities (logging, completion, pagination helpers)]

## Context
Implement required/declared server utilities and shared pagination behavior.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Utilities; Server utilities)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/logging.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/completion.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/pagination.md`

## Output / Definition of Done
* `logging/setLevel` supported when logging capability declared
* Server can emit `notifications/message` respecting configured level
* `completion/complete` supported when completions capability declared; max 100 values
* Pagination helper used consistently across all list endpoints

## Step-by-Step Instructions
1. Implement logging level model (RFC5424 strings) and filtering.
2. Implement `logging/setLevel` handler and notification emission API.
3. Implement `completion/complete` handler with reference types `ref/prompt` and `ref/resource`.
4. Implement cursor pagination helper and apply to tools/resources/prompts/tasks list endpoints.
5. Add tests for max-results constraint and pagination cursor opacity.

## Verification
* `ctest --test-dir build`
