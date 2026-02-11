# bidirectional_sampling_elicitation

Demonstrates server-initiated requests in a bidirectional MCP flow:
- `sampling/createMessage`
- `elicitation/create`
- task-augmented server request (`params.task`) followed by `tasks/result`

This example uses a simulated host-side responder so it can run standalone.

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_bidirectional_sampling_elicitation
```

## Run

```bash
./build/vcpkg-unix-release/examples/bidirectional_sampling_elicitation/mcp_sdk_example_bidirectional_sampling_elicitation
```
