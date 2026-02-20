# MCP Reference SDK Integration Coverage Matrix

This document tracks the coverage of MCP 2025-11-25 protocol surface items in the Python reference SDK integration tests.

## Protocol Surface Items

### Requests

| Protocol Item | Test Coverage |
|--------------|---------------|
| initialize | Pending |
| ping | Pending |
| tools/list | Pending |
| tools/call | Pending |
| resources/list | Pending |
| resources/read | Pending |
| resources/templates/list | Pending |
| resources/subscribe | Pending |
| resources/unsubscribe | Pending |
| prompts/list | Pending |
| prompts/get | Pending |
| logging/setLevel | Pending |
| completion/complete | Pending |
| roots/list | Pending |
| sampling/createMessage | Pending |
| elicitation/create | Pending |
| tasks/get | Pending |
| tasks/result | Pending |
| tasks/list | Pending |
| tasks/cancel | Pending |

### Notifications

| Protocol Item | Test Coverage |
|--------------|---------------|
| notifications/initialized | Pending |
| notifications/cancelled | Pending |
| notifications/progress | Pending |
| notifications/message | Pending |
| notifications/tools/list_changed | Pending |
| notifications/resources/list_changed | Pending |
| notifications/resources/updated | Pending |
| notifications/prompts/list_changed | Pending |
| notifications/roots/list_changed | Pending |
| notifications/tasks/status | Pending |
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
