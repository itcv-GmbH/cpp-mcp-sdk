# http_listen_example

Streamable HTTP example demonstrating server-initiated messages via GET SSE listen.

This example demonstrates:
- Streamable HTTP server setup with GET SSE support
- Client configuration with `enableGetListen = true`
- Roots provider setup for handling server-initiated requests

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
=== Creating Server ===
=== Configuring HTTP Server ===
[Server] HTTP server ready for requests

=== Creating Client ===
[Client] Configured with enableGetListen=true
[Client] Configured with roots provider for server-initiated requests

=== Demonstrating Server-Initiated Request Flow ===
[Demo] Server would use httpServer.enqueueServerMessage() to send
[Demo] a server-initiated roots/list request to the client
[Demo] Client's rootsProvider would be invoked to handle it
[Demo] Response would be sent back via the GET SSE connection

=== Shutting Down ===
=== Example completed successfully ===
```

## Code Flow

1. **Create Server**: Configure a server with tools capability
2. **Configure StreamableHttpServer**: Set up GET SSE (`allowGetSse = true`)
3. **Create Client**: Connect via HTTP with `enableGetListen = true`
4. **Set Roots Provider**: Register a callback for handling server-initiated roots/list requests
5. **Server sends message**: Would call `httpServer.enqueueServerMessage()` with a roots/list request
6. **Client responds**: Roots provider is invoked, returns root entries to the server

## Notes

- In a full implementation, the HTTP server would run on a proper server runtime (e.g., `StreamableHttpServerRunner`)
- The session ID is used to route server-initiated messages to the correct client
- If a server returns HTTP 405 for GET requests, the client falls back to POST-based message retrieval
