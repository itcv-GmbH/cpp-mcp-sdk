# Task ID: task-013
# Task Name: Add Top-Level Facades And Umbrella Headers

## Context
The SRS requires a minimal top-level API facade in `namespace mcp` and requires dedicated facade headers that expose `mcp::Client`, `mcp::Server`, and `mcp::Session` aliases.

The SRS also requires umbrella headers to exist only at module `all.hpp` paths and at `include/mcp/all.hpp`.

## Inputs
* `include/mcp/client/client.hpp`
* `include/mcp/server/server.hpp`
* `include/mcp/lifecycle/session.hpp`
* SRS: Functional Requirements 2 and 4

## Output / Definition of Done
* `include/mcp/client.hpp` is required to include `<mcp/client/client.hpp>` and declare `using Client = client::Client` in `namespace mcp`
* `include/mcp/server.hpp` is required to include `<mcp/server/server.hpp>` and declare `using Server = server::Server` in `namespace mcp`
* `include/mcp/session.hpp` is required to include `<mcp/lifecycle/session.hpp>` and declare `using Session = lifecycle::Session` in `namespace mcp`
* `include/mcp/all.hpp` is required to exist and is required to contain only `#include` directives

## Step-by-Step Instructions
1. Create `include/mcp/client.hpp` that includes `<mcp/client/client.hpp>` and declares the required alias in `namespace mcp`.
2. Create `include/mcp/server.hpp` that includes `<mcp/server/server.hpp>` and declares the required alias in `namespace mcp`.
3. Create `include/mcp/session.hpp` that includes `<mcp/lifecycle/session.hpp>` and declares the required alias in `namespace mcp`.
4. Create `include/mcp/all.hpp` and ensure it contains only `#include` directives.
5. Update include sites that previously relied on removed top-level wrapper headers to include canonical module headers or `include/mcp/all.hpp`.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
