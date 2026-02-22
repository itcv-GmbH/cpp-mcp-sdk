# Task ID: task-002
# Task Name: Normalize JSON-RPC Module Headers And Namespaces

## Context
This task will make JSON-RPC headers conform to canonical per-type and umbrella header rules.

The JSON-RPC module is a dependency of lifecycle, client, server, and transport layers, so its header layout and includes must be stabilized early.

## Inputs
* `include/mcp/jsonrpc/router.hpp`
* `include/mcp/jsonrpc/router_class.hpp`
* `include/mcp/jsonrpc/messages.hpp`
* `include/mcp/jsonrpc/progress_types.hpp`
* SRS: Functional Requirements 1, 3, and 4

## Output / Definition of Done
* `include/mcp/jsonrpc/router.hpp` is required to declare `mcp::jsonrpc::Router` and is required to not be an umbrella header
* `include/mcp/jsonrpc/router_class.hpp` must not exist
* `include/mcp/jsonrpc/messages.hpp` must not exist
* `include/mcp/jsonrpc/all.hpp` is required to exist and is required to contain only `#include` directives
* `include/mcp/jsonrpc/progress_types.hpp` must not exist
* A per-type header that declares `struct ProgressUpdate` is required to exist at `include/mcp/jsonrpc/progress_update.hpp`
* All `#include <mcp/jsonrpc/messages.hpp>` usages are required to be removed across `include/`, `src/`, `tests/`, `examples/`, and `docs/`

## Step-by-Step Instructions
1. Move the `mcp::jsonrpc::Router` declaration from `include/mcp/jsonrpc/router_class.hpp` into `include/mcp/jsonrpc/router.hpp`.
2. Delete `include/mcp/jsonrpc/router_class.hpp`.
3. Replace `include/mcp/jsonrpc/messages.hpp` with a module umbrella at `include/mcp/jsonrpc/all.hpp`.
4. Delete `include/mcp/jsonrpc/messages.hpp`.
5. Rename `include/mcp/jsonrpc/progress_types.hpp` to `include/mcp/jsonrpc/progress_update.hpp` and ensure the declared type remains `ProgressUpdate`.
6. Update all includes across the repository to use canonical JSON-RPC headers:
   - Code that needs broad JSON-RPC access is required to include `<mcp/jsonrpc/all.hpp>`.
   - Code that needs only Router is required to include `<mcp/jsonrpc/router.hpp>`.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
