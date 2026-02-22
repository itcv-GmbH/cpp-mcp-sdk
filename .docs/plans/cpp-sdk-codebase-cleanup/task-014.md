# Task ID: task-014
# Task Name: Split `include/mcp/lifecycle/session.hpp`

## Context
This task is responsible for converting `include/mcp/lifecycle/session.hpp` into an umbrella header and introducing per-type headers for its public `class` and `struct` types while preserving public API meaning.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/lifecycle/session.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/lifecycle/session.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/lifecycle/` for all top-level `class` and `struct` types formerly defined in `include/mcp/lifecycle/session.hpp`, including:
    *   `Executor`
    *   `SessionThreading`
    *   `SessionOptions`
    *   `RequestOptions`
    *   `LifecycleError`
    *   `CapabilityError`
    *   `Icon`
    *   `Implementation`
    *   `RootsCapability`
    *   `SamplingCapability`
    *   `ElicitationCapability`
    *   `TasksCapability`
    *   `LoggingCapability`
    *   `CompletionsCapability`
    *   `PromptsCapability`
    *   `ResourcesCapability`
    *   `ToolsCapability`
    *   `ClientCapabilities`
    *   `ServerCapabilities`
    *   `NegotiatedParameters`
    *   `Session`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the lifecycle module headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/lifecycle/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/lifecycle/session.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Update include dependencies so each per-type header compiles standalone.
5.  Build and run unit tests that cover lifecycle behavior.
6.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
