# Task ID: [task-003]
# Task Name: [Define Public API Surface + Core Types]

## Context
Define the SDK’s stable API boundaries early so the implementation can proceed in parallel (transports, auth, features) without rework.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Protocol Surface; Bidirectional Operation; Lifecycle)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts` (authoritative types and method set)

## Output / Definition of Done
* Public header skeletons exist under `include/mcp/` for core modules
* Namespaces and ownership model are clear (e.g., `mcp::Client`, `mcp::Server`, `mcp::Session`)
* A minimal “API overview” doc exists describing threading and handler model

## Step-by-Step Instructions
1. Define module boundaries and namespaces:
   - jsonrpc core (messages, router)
   - lifecycle session
   - transports (stdio, http)
   - server/client feature facades
   - auth
2. Draft key public types and callback signatures:
   - request handler registration API (method -> handler)
   - outbound request API returning a future/promise or callback-based completion
   - transport attach/start/stop
3. Specify threading model:
   - whether handlers run on IO threads or a separate executor
   - whether user must provide an executor
4. Add `include/mcp/version.hpp` with protocol version constants and negotiated version accessors.
5. Add `include/mcp/errors.hpp` with structured error types (JSON-RPC code/message/data).

## Verification
* `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
* `cmake --build build`
