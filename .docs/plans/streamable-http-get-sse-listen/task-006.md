# Task ID: task-006
# Task Name: Update Examples And Documentation For HTTP Listen

## Context

The SDK must provide an example that demonstrates server-initiated messages over Streamable HTTP. This task will update or add an example that:

* starts an HTTP server
* connects an HTTP client
* demonstrates a server-initiated request delivered over GET SSE

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` section: "Streamable HTTP GET (Server-Initiated Messages)"
* `examples/`
* `include/mcp/client/client.hpp`
* `include/mcp/server/server.hpp`

## Output / Definition of Done

* At least one example under `examples/` will demonstrate server-initiated messaging over HTTP.
* The example will document required steps for:
  - enabling GET SSE listen behavior
  - configuring a roots provider and/or sampling/elicitation handlers
* The example will build under `cmake --build build/vcpkg-unix-release`.

## Step-by-Step Instructions

1. Add a new example or update an existing example to run a Streamable HTTP server and client in the same process.
2. The example must:
   - complete initialization
   - enqueue a server-to-client request
   - show that the client responds correctly
3. Add README text in the example folder describing how to run it.

## Verification

* `cmake --build build/vcpkg-unix-release --target mcp_sdk_example_*`
