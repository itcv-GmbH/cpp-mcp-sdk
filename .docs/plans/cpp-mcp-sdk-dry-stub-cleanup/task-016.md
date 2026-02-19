# Task ID: task-016
# Task Name: Router Detached Thread Removal

## Context
`jsonrpc::Router::dispatchRequest()` currently spawns a detached thread per inbound request to await the handler future and complete bookkeeping. This complicates shutdown and can cause hangs or resource leaks in hosts that create/destroy routers repeatedly. The router already has shutdown coordination (`markInboundWorkerStarted/Finished`, `waitForInboundWorkers`).

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Performance/Reliability; shutdown expectations)
*   `include/mcp/jsonrpc/router.hpp`
*   `src/jsonrpc/router.cpp` (detached thread usage)
*   Existing router tests: `tests/jsonrpc_router_test.cpp`

## Output / Definition of Done
*   `src/jsonrpc/router.cpp` no longer uses `std::thread(...).detach()` for inbound request completion.
*   A managed execution mechanism is used instead:
    *   Option A: store joinable `std::thread` objects and join them during router shutdown/destruction, or
    *   Option B: use a bounded worker pool (e.g., `boost::asio::thread_pool`) dedicated to waiting on handler futures and finalizing request state.
*   Router shutdown/destructor cannot hang waiting for detached threads.
*   `tests/jsonrpc_router_test.cpp` includes a regression test proving Router destruction completes promptly with in-flight requests.

## Step-by-Step Instructions
1.  Identify the detached thread site in `src/jsonrpc/router.cpp` inside `Router::dispatchRequest()`.
2.  Choose one managed execution strategy:
    *   Option A (minimal API impact): add a private `std::vector<std::thread>` guarded by `mutex_`; push the worker thread there; on shutdown, signal and join all joinable threads.
    *   Option B (more scalable): add a private `boost::asio::thread_pool` for inbound completion work; `post()` a task that waits on `handlerFuture.get()` and then finalizes state.
3.  Ensure worker accounting remains correct:
    *   `markInboundWorkerStarted()` must fail fast when shutting down.
    *   `markInboundWorkerFinished()` must be called exactly once per accepted inbound request.
    *   `completeInboundRequest()` must still run even if handler throws.
4.  Update Router destructor/shutdown path to reliably stop the worker mechanism before returning.
5.  Add/extend `tests/jsonrpc_router_test.cpp`:
    *   create a request handler that blocks on a promise/future
    *   dispatch a request, then destroy the router while the handler is still blocked
    *   assert destruction completes within a bounded timeout (use `std::async` + `wait_for` and fail fast)

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_jsonrpc_router_test --output-on-failure`
