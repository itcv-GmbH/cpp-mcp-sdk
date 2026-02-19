# Task ID: task-013
# Task Name: Clarify StdioTransport Instance API

## Context
`transport::StdioTransport` mixes (1) useful static framing/subprocess utilities and (2) a `Transport` implementation whose constructors/options are currently ignored and whose attach method is a no-op. This is confusing for SDK users and makes it easy to misuse the API.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (stdio transport required; subprocess shutdown requirements)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
*   `include/mcp/transport/stdio.hpp`
*   `src/transport/stdio.cpp`
*   Existing tests using static APIs: `tests/transport_stdio_test.cpp`, `tests/transport_stdio_subprocess_test.cpp`, `tests/conformance/test_stdio_transport.cpp`

## Output / Definition of Done
*   Instance-level `StdioTransport` API is no longer misleading:
    *   Either deprecate the instance constructors / `Transport`-virtual methods, or make them functional and documented.
*   Static utilities (`run`, `attach`, `routeIncomingLine`, `spawnSubprocess`) remain supported and unchanged.
*   Add a regression test (or extend an existing one) to prevent reintroducing “ignored options” behavior.

## Step-by-Step Instructions
1.  Decide the desired public contract:
    *   Option A (preferred): Treat `StdioTransport` as a static utility + subprocess helper and deprecate instance `Transport` usage.
    *   Option B: Implement a real bidirectional `Transport` for stdio (requires clear blocking/threading semantics).
2.  Implement the chosen option:
    *   For Option A: add `[[deprecated]]` attributes and make instance methods throw with guidance to use `Client::connectStdio` / `StdioTransport::run`.
    *   For Option B: wire attach/start/stop/send to an internal reader thread and session/router integration.
3.  Update/extend tests to validate the new contract.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_stdio_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_stdio_subprocess_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_stdio_transport_test --output-on-failure`
