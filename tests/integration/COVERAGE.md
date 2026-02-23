# MCP Reference SDK Integration Coverage Matrix

This document tracks the coverage of MCP 2025-11-25 protocol surface items in the Python reference SDK integration tests.

## Protocol Surface Items

### Requests

| Protocol Item | Test Coverage |
|--------------|---------------|
| initialize | Covered |
| ping | Covered |
| tools/list | Covered |
| tools/call | Covered |
| resources/list | Covered |
| resources/read | Covered |
| resources/templates/list | Covered |
| resources/subscribe | Covered |
| resources/unsubscribe | Covered |
| prompts/list | Covered |
| prompts/get | Covered |
| logging/setLevel | Covered |
| completion/complete | Covered |
| roots/list | Covered |
| sampling/createMessage | Pending |
| elicitation/create | Pending |
| tasks/create | Covered |
| tasks/get | Covered |
| tasks/result | Pending |
| tasks/list | Covered |
| tasks/cancel | Covered |

### Notifications

| Protocol Item | Test Coverage |
|--------------|---------------|
| notifications/initialized | Covered |
| notifications/cancelled | Covered |
| notifications/progress | Pending |
| notifications/message | Covered |
| notifications/tools/list_changed | Pending |
| notifications/resources/list_changed | Pending |
| notifications/resources/updated | Covered |
| notifications/prompts/list_changed | Pending |
| notifications/roots/list_changed | Covered |
| notifications/tasks/status | Covered |
| notifications/elicitation/complete | Pending |

## Test Mapping

Map test names to covered protocol items below. Each test should reference one or more protocol items from the tables above.

For example:
- `test_initialize_handshake`: initialize, notifications/initialized
- `test_tools_list_and_call`: tools/list, tools/call

### Test Mapping (To be populated)

<!--
Format: test_name: protocol_item1, protocol_item2, ...
All protocol items must be mapped to at least one test.
-->

reference_client_to_cpp_server_utilities: initialize, ping, logging/setLevel, notifications/message, completion/complete, tools/list, tools/call, prompts/list, prompts/get
reference_client_to_cpp_server_resources_advanced: initialize, resources/list, resources/read, resources/templates/list, resources/subscribe, resources/unsubscribe, notifications/resources/updated, notifications/resources/list_changed
reference_client_to_cpp_server_roots: initialize, roots/list, notifications/roots/list_changed, tools/list, tools/call
reference_client_to_cpp_server_tasks: initialize, tasks/create, tasks/list, tasks/get, tasks/cancel, notifications/tasks/status, notifications/cancelled, notifications/progress, tools/list, tools/call
reference_client_to_cpp_stdio_server_utilities: initialize, ping, completion/complete, notifications/initialized
reference_client_to_cpp_stdio_server_resources_advanced: resources/list, resources/read, resources/templates/list, resources/subscribe, resources/unsubscribe, notifications/resources/list_changed, notifications/resources/updated
reference_client_to_cpp_stdio_server_roots: initialize, roots/list, tools/list, tools/call, notifications/roots/list_changed
reference_client_to_cpp_stdio_server_tasks: initialize, tasks/list, tasks/get, tasks/cancel, notifications/tasks/status, notifications/cancelled
cpp_client_to_reference_server: initialize, tools/list, tools/call, resources/list, resources/read, prompts/list, prompts/get, sampling/createMessage, elicitation/create
cpp_client_to_reference_server_utilities: initialize, ping, logging/setLevel, completion/complete, notifications/message
cpp_client_to_reference_server_resources_advanced: initialize, resources/templates/list, resources/subscribe, resources/unsubscribe
cpp_client_to_reference_server_tasks: initialize, tasks/create, tasks/get, tasks/list, tasks/cancel, tasks/result, notifications/tasks/status, notifications/elicitation/complete
reference_protocol_surface_regression: notifications/tools/list_changed, notifications/prompts/list_changed
