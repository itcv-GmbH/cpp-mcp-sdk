# http_listen_example

Streamable HTTP example demonstrating server-initiated messages via GET SSE listen.

This example demonstrates:
- In-process StreamableHttpServer setup with GET SSE support
- Client configuration with `enableGetListen = true`
- Roots provider setup for handling server-initiated requests
- Complete round-trip: server enqueues `roots/list` → client receives via GET SSE → roots provider invoked → response sent back

## What is GET SSE Listen?

The MCP 2025-11-25 transport specification introduced "Listening for Messages from the Server" - a mechanism for servers to send requests to clients without waiting for a client request. This is achieved via HTTP GET SSE (Server-Sent Events):

- **Client-side**: Configure `HttpClientOptions.enableGetListen = true` to enable GET requests for receiving server-initiated messages
- **Server-side**: Use `StreamableHttpServer::enqueueServerMessage()` to queue messages for delivery to connected clients

## Key Concepts

### Server-Initiated Requests

Normally MCP follows a request-response pattern where only clients send requests. With GET SSE listen, servers can send:
- `roots/list` - request the client's roots
- `sampling/createMessage` - request the client to sample from an LLM
- `elicitation/create` - request user input from the client

### Roots Provider

When a client sets up a roots provider via `Client::setRootsProvider()`, it becomes capable of responding to server-initiated `roots/list` requests. This is essential for server-initiated messaging workflows.

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

Server-initiated message flow demonstrated:
  1. Server returned useSse=true in initialize response
  2. Client enabled GET SSE listen
  3. Server called enqueueServerMessage(roots/list)
  4. Client received request via GET SSE
  5. Client invoked roots provider callback
  6. Client sent response back to server
  7. Server received and validated response
```

## Code Flow

1. **Create In-Process Server**: Create a `StreamableHttpServer` instance to handle requests
2. **Configure Server Handlers**: Set up request handler to return `useSse=true` in initialize response
3. **Create Client**: Connect via HTTP with `enableGetListen = true`
4. **Set Roots Provider**: Register a callback for handling server-initiated roots/list requests
5. **Initialize**: Client sends initialize request, server returns roots capability + useSse=true
6. **Server Enqueues Message**: Server calls `server.enqueueServerMessage(roots/list request)`
7. **Client Processes**: Client receives request via GET SSE, invokes roots provider
8. **Response Sent Back**: Client sends response with roots data to server
9. **Server Verifies**: Server validates response contains expected roots data
