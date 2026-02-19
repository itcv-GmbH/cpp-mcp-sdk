# Task ID: task-009
# Task Name: Refactor HTTP Client Endpoint URL Parsing

## Context
Streamable HTTP client code parses endpoint URLs and normalizes request targets with local logic. This is closely related to (and should be consistent with) the shared absolute URL parsing used by auth flows.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP requirements; required headers; session management)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
*   URL parsing logic in:
    *   `src/transport/http_client.cpp`
    *   `src/transport/http_runtime.cpp`
*   Shared helpers from `include/mcp/detail/ascii.hpp` and `include/mcp/detail/url.hpp`

## Output / Definition of Done
*   `src/transport/http_client.cpp` updated to use the shared URL parser for endpoint URL parsing.
*   `src/transport/http_runtime.cpp` updated similarly.
*   Existing behavior is preserved (http/https only; default ports; normalized request target; userinfo rejected).
*   Streamable HTTP conformance tests remain green.

## Step-by-Step Instructions
1.  Replace `parseHttpEndpointUrl()` in `src/transport/http_client.cpp` with a wrapper around the shared absolute URL parser.
2.  Preserve the existing request-target normalization behavior (`/` default; strip fragments; prefix `/` when needed).
3.  Apply the same refactor in `src/transport/http_runtime.cpp`.
4.  Add a small set of unit tests in an existing HTTP transport test file if required to lock in endpoint parsing (avoid duplicating the full parser tests).

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_common_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_streamable_http_transport_test --output-on-failure`
