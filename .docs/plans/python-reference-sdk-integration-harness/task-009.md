# Task ID: task-009
# Task Name: Implement C++ STDIO Resources Advanced Fixture

## Context
This task will add a dedicated C++ STDIO server fixture that exercises resource templates, resource subscriptions, and resource update notifications against the Python reference client over STDIO.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Requests: `resources/templates/list`, `resources/subscribe`, `resources/unsubscribe`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/resources.md`
* Existing fixture: `tests/integration/cpp_stdio_server_fixture.cpp`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_stdio_server_resources_advanced_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_stdio_server_resources_advanced`.
* The fixture implements templates list, subscribe, unsubscribe.
* The fixture emits `notifications/resources/updated` and `notifications/resources/list_changed`.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_stdio_server_resources_advanced_fixture.cpp`.
2. Implement `resources/templates/list` and at least one templated resource.
3. Implement `resources/subscribe` and `resources/unsubscribe`.
4. Emit `notifications/resources/updated` for a subscribed resource.
5. Emit `notifications/resources/list_changed` for a resource list mutation.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
