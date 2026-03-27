# bidirectional_sampling_elicitation

This example demonstrates server-initiated requests in a **bidirectional** MCP flow. 

While clients normally drive the interaction, MCP allows servers to request actions from the client. This example showcases:
- **Sampling (`sampling/createMessage`)**: The server asks the client to use its connected LLM to generate text or sample a response based on a prompt.
- **Elicitation (`elicitation/create`)**: The server asks the client to prompt the user for input (e.g., via a form) and return the user's response.
- **Tasks (`tasks/result`)**: The server provides a task ID, and the client reports task progress/results.

## Architecture & Code Flow

1. **Standalone Execution**: This example uses a simulated host-side responder (acting as the "client") so it can run standalone without needing an actual external LLM client connected.
2. **Capabilities**: The client connects and declares it supports `sampling` and `elicitation`.
3. **Outbound Message Sender**: The server configures an `OutboundMessageSender` callback. This intercepts JSON-RPC requests that the server wants to send *to* the client.
4. **Mock Client Processing**: When the server asks for an LLM sample (`sampling/createMessage`), the mock client intercepts the request and instantly replies with a simulated LLM response ("Mock LLM response").
5. **Completion**: The server receives the simulated client response and prints the results.

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_bidirectional_sampling_elicitation
```

## Run

```bash
./build/vcpkg-unix-release/examples/bidirectional_sampling_elicitation/mcp_sdk_example_bidirectional_sampling_elicitation
```