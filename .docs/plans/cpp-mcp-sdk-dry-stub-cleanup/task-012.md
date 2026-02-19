# Task ID: task-012
# Task Name: Remove or Hard-Disable HttpTransport Stub

## Context
`transport::HttpTransport` is a public class that currently drops outbound messages (no-op send). This is “API slop” and dangerous: users can believe they have a working HTTP transport when they do not. The SDK already exposes real Streamable HTTP primitives (`transport::http::StreamableHttpClient/Server` and `Http*Runtime`).

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP required)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
*   `include/mcp/transport/http.hpp` (declares `HttpTransport`)
*   `src/transport/http_server.cpp` (stub implementation)
*   `tests/smoke_test.cpp` (API surface sanity check)

## Output / Definition of Done
*   One of the following strategies implemented (pick based on SDK API policy):
    *   Strategy A (preferred): Remove `transport::HttpTransport` entirely from the public header and delete its implementation.
    *   Strategy B: Mark `transport::HttpTransport` as `[[deprecated]]` and make all runtime usage fail fast with an actionable exception (no silent drops).
*   `tests/smoke_test.cpp` updated accordingly.
*   If Strategy B is chosen: add/extend a unit test to assert `HttpTransport::send()` throws.

## Step-by-Step Instructions
1.  Confirm `transport::HttpTransport` is not used by examples/tests other than API surface compilation.
2.  Implement Strategy A or B.
3.  If removing, ensure downstream public API still provides a clear HTTP story:
    *   client: `Client::connectHttp(...)`
    *   server: `transport::http::StreamableHttpServer` + `HttpServerRuntime` integration
4.  Update `tests/smoke_test.cpp` to reflect the new API surface.
5.  If Strategy B, add a regression test so future changes can’t reintroduce a silent no-op transport.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_smoke_test --output-on-failure`
