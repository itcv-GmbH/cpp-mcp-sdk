# Task ID: task-014
# Task Name: Expand Python Reference Server Fixture

## Context
The existing Python reference server fixture supports tools, resources, prompts, and server-initiated sampling and elicitation checks. This task will expand the reference server fixture to expose the remaining protocol surface so the C++ client fixtures will validate interop end-to-end.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Full protocol surface list)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/logging.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/completion.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/resources.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`
* `tests/integration/fixtures/reference_python_server.py`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/fixtures/reference_python_server.py` implements:
  - `ping`
  - `logging/setLevel` and `notifications/message`
  - `completion/complete`
  - `resources/templates/list`
  - `resources/subscribe` and `resources/unsubscribe`
  - `notifications/resources/updated` and `notifications/resources/list_changed`
  - tasks utility request handling and `notifications/tasks/status`
* The fixture prints readiness information to stdout with the final endpoint.

## Step-by-Step Instructions
1. Extend `tests/integration/fixtures/reference_python_server.py` to register the missing handlers using the reference SDK facilities.
2. Implement deterministic triggers that cause the server to emit required notifications.
3. Implement deterministic state so the C++ client fixtures must assert stable results and stable notification sequences.
4. Update `tests/integration/README.md` to document the expanded Python reference server fixture behavior.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_integration_reference_cpp_client_to_reference_server --output-on-failure`
