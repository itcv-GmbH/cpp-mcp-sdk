# Task ID: task-005
# Task Name: Implement C++ HTTP Resources Advanced Fixture

## Context
This task will add a dedicated C++ Streamable HTTP fixture that exercises resource templates, resource subscriptions, and resource update notifications against the Python reference client.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Requests: `resources/templates/list`, `resources/subscribe`, `resources/unsubscribe`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/resources.md`
* Existing fixture: `tests/integration/cpp_server_fixture.cpp`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_server_resources_advanced_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_server_resources_advanced`.
* The fixture exposes at least one resource template and returns it via `resources/templates/list`.
* The fixture accepts `resources/subscribe` and `resources/unsubscribe` and records subscription state.
* The fixture emits `notifications/resources/updated` for a subscribed resource and emits `notifications/resources/list_changed` for a resource set mutation.
* The fixture emits deterministic, machine-parseable markers to stdout for key events.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_server_resources_advanced_fixture.cpp` and implement a Streamable HTTP server fixture with token verification.
2. Register:
   - one resource template for `resources/templates/list`
   - one concrete resource for `resources/list` and `resources/read`
3. Implement subscription handlers:
   - `resources/subscribe` must return success and mark the subscription active
   - `resources/unsubscribe` must return success and mark the subscription inactive
4. Implement a deterministic trigger that updates a subscribed resource and emits `notifications/resources/updated`.
5. Implement a deterministic trigger that mutates the resource list and emits `notifications/resources/list_changed`.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
