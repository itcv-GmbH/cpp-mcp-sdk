# Integration Tests (Reference SDK Interop)

This directory contains end-to-end integration tests that exercise interoperability between this C++ SDK and the official Python reference SDK over Streamable HTTP.

## Pinned Reference SDK Versions

- `mcp==1.26.0`
- `httpx==0.28.1`

The pinned versions are captured in `tests/integration/fixtures/reference_python_requirements.txt`.

## Reference Python Server Capabilities

The reference Python server fixture (`reference_python_server.py`) exposes the following capabilities:

### Tools

| Tool | Description |
|------|-------------|
| `python_echo` | Echo text provided by the caller |
| `ping` | Ping the server to check connectivity |
| `logging_setLevel` | Set logging level and emit notification |
| `completion_complete` | Return completion suggestions |
| `tasks_create` | Create a new background task |
| `tasks_list` | List all background tasks |
| `tasks_get` | Get a specific task by ID |
| `tasks_cancel` | Cancel a running task |

### Resources

| Resource URI | Description |
|--------------|-------------|
| `resource://python-server/info` | Reference server metadata |
| `resource://python-server/template/{item_id}` | Template resource with dynamic item_id |

### Prompts

| Prompt | Description |
|--------|-------------|
| `python_server_prompt` | Returns a prompt containing the supplied topic |

### Notifications

The server emits `notifications/message` notifications for:
- Log level changes (`logging_setLevel`)
- Task creation, cancellation, and status updates

### Protocol Handlers

- Sampling: Server can request the client to sample LLM responses
- Elicitation: Server can request user approval through the client

## Coverage

- Reference Python client -> C++ SDK server fixture (Streamable HTTP runner)
  - unauthenticated initialize expected to fail with authorization semantics
  - authenticated initialize succeeds and returns server-issued `MCP-Session-Id`
  - each initialize request receives a unique `MCP-Session-Id`
  - non-initialize requests without `MCP-Session-Id` return HTTP 400 when `requireSessionId=true`
  - tools/resources/prompts work end-to-end
  - C++ server initiates `sampling/createMessage` and `elicitation/create`; reference client handlers respond and server-side assertions enforce explicit timeouts
- Reference Python client -> C++ SDK STDIO server fixture (STDIO runner)
  - tools/resources/prompts work end-to-end
  - C++ server initiates `sampling/createMessage` and `elicitation/create`; reference client handlers respond and server-side assertions enforce explicit timeouts
- C++ SDK client fixture -> reference Python server
  - unauthenticated initialize is explicitly verified as HTTP `401` with `WWW-Authenticate: Bearer ...`
  - C++ client unauthorized path additionally requires `401`/authorization evidence (not just generic initialize failure)
  - authenticated initialize succeeds
  - tools/resources/prompts work end-to-end
  - reference server initiates `sampling/createMessage` and `elicitation/create`; C++ client handlers respond and server-side assertions enforce explicit timeouts

## Verification

- `cmake -S . -B build/vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
- `cmake --build build/vcpkg-unix-release`
- `ctest --test-dir build/vcpkg-unix-release -R integration_reference --output-on-failure`

## Manual Testing

To test the reference Python server manually:

```bash
cd tests/integration
python3 fixtures/reference_python_server.py --port 8765 --path /mcp
```
