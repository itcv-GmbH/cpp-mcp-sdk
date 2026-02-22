# Task ID: task-004
# Task Name: Normalize HTTP SSE Module Layout

## Context
The repository currently exposes SSE types under `namespace mcp::http::sse` while placing headers directly under `include/mcp/http/`.

This task will relocate headers so that directory layout mirrors namespace layout and will remove prohibited umbrella headers under `include/mcp/http/`.

## Inputs
* `include/mcp/http/sse.hpp`
* `include/mcp/http/detail.hpp`
* `include/mcp/http/event.hpp`
* `include/mcp/http/event_id_cursor.hpp`
* `include/mcp/http/encoding.hpp`
* SRS: Functional Requirements 1, 4, 5, and 7

## Output / Definition of Done
* `include/mcp/http/all.hpp` is required to exist and is required to contain only `#include` directives
* SSE public headers are required to exist under `include/mcp/http/sse/` and declare symbols in `namespace mcp::http::sse`
* SSE detail headers are required to exist under `include/mcp/http/sse/detail/` and declare symbols in `namespace mcp::http::sse::detail`
* `include/mcp/http/sse.hpp` must not exist
* `include/mcp/http/detail.hpp` must not exist

## Step-by-Step Instructions
1. Create directories `include/mcp/http/sse/` and `include/mcp/http/sse/detail/`.
2. Relocate SSE public headers from `include/mcp/http/` into `include/mcp/http/sse/`:
   - `event.hpp`
   - `event_id_cursor.hpp`
   - `encoding.hpp`
3. Relocate SSE internal helpers currently in `include/mcp/http/detail.hpp` into `include/mcp/http/sse/detail/` and preserve `namespace mcp::http::sse::detail`.
4. Delete `include/mcp/http/sse.hpp` and replace all includes of it with `<mcp/http/all.hpp>` or with the new per-type headers.
5. Create `include/mcp/http/all.hpp` as the only HTTP module umbrella and ensure it contains only `#include` directives.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
