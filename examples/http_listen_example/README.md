# http_listen_example

Streamable HTTP example demonstrating server-initiated messages via GET SSE listen.

This example demonstrates:
- In-process `StreamableHttpServer` setup with GET SSE (Server-Sent Events) support
- Client configuration with `enableGetListen = true`
- Roots provider setup for handling server-initiated requests
- Complete round-trip: server enqueues `roots/list` → client receives via GET SSE → roots provider invoked → response sent back

## What is GET SSE Listen?

The MCP 2025-11-25 transport specification introduced "Listening for Messages from the Server" - a mechanism for servers to send requests to clients without waiting for a client request. This is achieved via HTTP GET SSE (Server-Sent Events):

- **Client-side**: Configure `HttpClientOptions.enableGetListen = true` to enable GET requests for receiving server-initiated messages.
- **Server-side**: Use `StreamableHttpServer::enqueueServerMessage()` to queue messages for delivery to connected clients.

## Key Concepts

### Server-Initiated Requests
Normally MCP follows a request-response pattern where only clients send requests. With GET SSE listen, servers can send requests to the client, such as:
- `roots/list` - request the client's roots (directories the server is allowed to access)
- `sampling/createMessage` - request the client to sample from an LLM
- `elicitation/create` - request user input from the client

### Roots Provider
When a client sets up a roots provider via `client->setRootsProvider(...)`, it becomes capable of responding to server-initiated `roots/list` requests. This is essential for server-initiated messaging workflows.

## Code Flow

1. **Create In-Process Server**: Create a `StreamableHttpServer` instance to handle requests.
2. **Configure Server Handlers**: Set up a request handler to return `useSse=true` in the initialize response.
3. **Create Client**: Connect via HTTP with `enableGetListen = true`.
4. **Set Roots Provider**: Register a callback for handling server-initiated `roots/list` requests.
5. **Initialize**: Client sends the `initialize` request, server returns roots capability + `useSse=true`.
6. **Server Enqueues Message**: Server calls `server.enqueueServerMessage()` requesting `roots/list`.
7. **Client Processes**: Client receives the request via GET SSE and invokes the roots provider callback.
8. **Response Sent Back**: Client sends the response with roots data back to the server.
9. **Server Verifies**: Server validates that the response contains the expected roots data.

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_http_listen
```

## Run

```bash
./build/vcpkg-unix-release/examples/http_listen_example/mcp_sdk_example_http_listen
```

## Expected Output
```
=== Creating In-Process HTTP Server ===

=== Creating Client ===
[Client] Initializing connection...
[Server] Received notifications/initialized from client
[Client] Initialization successful

=== Sending Server-Initiated Request ===
[Server] Enqueueing roots/list request...
[Server] Enqueue result: success
[Client] Roots provider invoked (server-initiated request received)
[Server] Received response from client

=== Verifying Response ===
[Server] Received success response
[Server] Response contains 1 root(s)
[Server]   - uri: file:///example/dynamic-resource
[Server]     name: Dynamic Resource

=== Shutting Down ===
=== Example completed successfully ===
```