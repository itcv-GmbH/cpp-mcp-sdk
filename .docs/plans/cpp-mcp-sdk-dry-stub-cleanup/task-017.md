# Task ID: task-017
# Task Name: Client Detached Thread Removal

## Context
`mcp::Client` uses detached threads for async callbacks (e.g., `Client::sendRequestAsync`) and for some background work. Detached threads make shutdown/destruction harder to reason about and can leak work past `Client::stop()`.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Reliability; graceful shutdown)
*   `include/mcp/client/client.hpp`
*   `src/client/client.cpp` (detached thread usage)
*   Existing tests: `tests/client_test.cpp`
*   Output of `task-016` (preferred: reuse the same managed execution approach/pool model)

## Output / Definition of Done
*   `src/client/client.cpp` no longer uses `std::thread(...).detach()`.
*   `Client::stop()` (and/or destructor) ensures any internally-started background work is terminated or joined.
*   A regression test in `tests/client_test.cpp` proves that creating a client, issuing at least one async request/callback, and destroying/stopping the client does not hang.

## Step-by-Step Instructions
1.  Locate detached thread sites in `src/client/client.cpp` (at minimum `Client::sendRequestAsync`).
2.  Pick a managed execution approach consistent with `task-016`:
    *   Option A: store joinable threads in `Client` and join them in `stop()` (or destructor) under a mutex.
    *   Option B: introduce a small internal worker pool (e.g., `boost::asio::thread_pool`) for callback continuations.
3.  Define shutdown semantics:
    *   after `stop()`, no new async callbacks should be started
    *   in-flight callbacks should either complete promptly or be cancelled (document which)
4.  Update `tests/client_test.cpp`:
    *   construct a client with a transport fixture that returns a controlled response
    *   call `sendRequestAsync` with a callback that signals completion
    *   call `stop()` and destroy the client; assert completion and no hang

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_client_test --output-on-failure`
