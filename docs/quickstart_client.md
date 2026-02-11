# Quickstart: MCP Client

This quickstart covers:

- stdio client setup against the stdio server example
- HTTP auth flow example with discovery + PKCE + loopback redirect receiver
- bidirectional server-initiated requests (sampling/elicitation/tasks)

## Build the client-related examples

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target \
  mcp_sdk_example_http_client_auth \
  mcp_sdk_example_bidirectional_sampling_elicitation \
  mcp_sdk_example_stdio_server
```

## Stdio client pattern

There is no standalone stdio client binary in `examples/`; the API path is `mcp::Client::connectStdio`.

Minimal pattern:

```cpp
#include <mcp/client/client.hpp>
#include <mcp/transport/stdio.hpp>

auto client = mcp::Client::create();

mcp::transport::StdioClientOptions stdioOptions;
stdioOptions.executablePath = "./build/vcpkg-unix-release/examples/stdio_server/mcp_sdk_example_stdio_server";

client->connectStdio(stdioOptions);
client->start();
static_cast<void>(client->initialize().get());
// initialize() completes the handshake and sends notifications/initialized.

const auto tools = client->listTools();
client->stop();
```

Lifecycle ordering reminder for request-based APIs:

1. `initialize`
2. `notifications/initialized`
3. normal requests (for example `tools/list`)

If you bypass `Client::initialize()` and send lifecycle messages manually, use:

```cpp
client->sendNotification("notifications/initialized");
```

before calling request methods such as `listTools()`.

## HTTP auth + loopback receiver example

Run the complete local flow:

```bash
./build/vcpkg-unix-release/examples/http_client_auth/mcp_sdk_example_http_client_auth
```

What this example demonstrates (`examples/http_client_auth/main.cpp`):

- RFC9728 protected resource metadata discovery
- RFC8414 authorization server metadata discovery
- PKCE code verifier/challenge generation
- loopback redirect capture via `mcp::auth::LoopbackRedirectReceiver`
- token exchange request generation and policy-controlled execution

Expected terminal output includes lines like:

- `discovered metadata:`
- `authorization URL: ...`
- `loopback callback status: 200`
- `authorization code received: ...`
- `token response status: 200`

Important: this example is local-only and relaxes discovery policy checks:

- `requireHttps = false`
- `allowPrivateAndLocalAddresses = true`

Do not use those relaxed settings in production.

## Host responsibility in OAuth login

In production, the host application is responsible for opening the authorization URL in the user agent and waiting for the loopback callback. The SDK provides building blocks (`buildAuthorizationUrl`, `LoopbackRedirectReceiver`, `buildTokenExchangeHttpRequest`) but does not open a browser for you.

## Bidirectional flow sample (client handlers)

Run:

```bash
./build/vcpkg-unix-release/examples/bidirectional_sampling_elicitation/mcp_sdk_example_bidirectional_sampling_elicitation
```

This demonstrates server-initiated requests and task-augmented flow:

- `sampling/createMessage`
- `tasks/result`
- `elicitation/create`

Use this as the reference for wiring `setSamplingCreateMessageHandler`, `setFormElicitationHandler`, `setUrlElicitationHandler`, and task-capable request handling in client code.
