# C++ MCP SDK (Full-Spec Conformant, Cross-Platform)

## Background

We need a production-usable C++ library (SDK) that implements the full Model Context Protocol (MCP) specification (including remote connectivity and authorization) and that builds and runs on Linux, macOS, and Windows.

This SDK will be used to build MCP servers and/or MCP clients in native C++ applications, with interoperability across the broader MCP ecosystem (official SDKs, MCP Inspector, and third-party hosts).

Primary standards sources:

- MCP versioning and negotiation: https://modelcontextprotocol.io/specification/versioning
- MCP 2025-11-25 specification (current): https://modelcontextprotocol.io/specification/2025-11-25/
- MCP protocol TypeScript schema (source of truth): https://github.com/modelcontextprotocol/specification/blob/main/schema/2025-11-25/schema.ts
- MCP protocol JSON Schema (generated from TS): https://github.com/modelcontextprotocol/specification/blob/main/schema/2025-11-25/schema.json
- Transports (stdio + Streamable HTTP): https://modelcontextprotocol.io/specification/2025-11-25/basic/transports
- Base protocol overview (JSON-RPC, lifecycle, auth pointers): https://modelcontextprotocol.io/specification/2025-11-25/basic
- Lifecycle: https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle
- Authorization (OAuth-based, HTTP only): https://modelcontextprotocol.io/specification/2025-11-25/basic/authorization
- Security best practices (token passthrough, session hijacking, DNS rebinding, etc.): https://modelcontextprotocol.io/specification/2025-11-25/basic/security_best_practices
- Server features:
  - Tools: https://modelcontextprotocol.io/specification/2025-11-25/server/tools
  - Resources: https://modelcontextprotocol.io/specification/2025-11-25/server/resources
  - Prompts: https://modelcontextprotocol.io/specification/2025-11-25/server/prompts
- Client features:
  - Roots: https://modelcontextprotocol.io/specification/2025-11-25/client/roots
  - Sampling: https://modelcontextprotocol.io/specification/2025-11-25/client/sampling
  - Elicitation: https://modelcontextprotocol.io/specification/2025-11-25/client/elicitation
- Utilities:
  - Ping: https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/ping
  - Cancellation: https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/cancellation
  - Progress: https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/progress
  - Tasks: https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/tasks
- Server utilities:
  - Logging: https://modelcontextprotocol.io/specification/2025-11-25/server/utilities/logging
  - Completion: https://modelcontextprotocol.io/specification/2025-11-25/server/utilities/completion
  - Pagination: https://modelcontextprotocol.io/specification/2025-11-25/server/utilities/pagination
- Schema reference: https://modelcontextprotocol.io/specification/2025-11-25/schema
- Legacy transport reference (HTTP+SSE in 2024-11-05): https://modelcontextprotocol.io/specification/2024-11-05/basic/transports

Local normative references for this project (pinned snapshot):

- MCP spec prose (mirrored): `.docs/requirements/mcp-spec-2025-11-25/spec/`
- MCP schemas (mirrored): `.docs/requirements/mcp-spec-2025-11-25/schema/`
- Mirror manifest (defines completeness): `.docs/requirements/mcp-spec-2025-11-25/MANIFEST.md`

Local pinned mirror for review and conformance:

- MCP spec mirror index: `.docs/requirements/mcp-spec-2025-11-25/README.md`
- MCP spec prose root (mirrored): `.docs/requirements/mcp-spec-2025-11-25/spec/`
- MCP spec mirror manifest (completeness checklist): `.docs/requirements/mcp-spec-2025-11-25/MANIFEST.md`
- MCP schema (TS): `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts`
- MCP schema (JSON): `.docs/requirements/mcp-spec-2025-11-25/schema/schema.json`

## User Stories

- As a C++ application developer, I want to embed an MCP server so that an MCP-capable host (e.g., an AI assistant) can discover and call my tools and read my resources.
- As a C++ application developer, I want to embed an MCP client so that my app can connect to MCP servers and invoke their tools/resources/prompts.
- As a C++ application developer, I want the SDK to support bidirectional MCP (server-initiated requests like sampling/elicitation) so that agentic servers can ask the host client to sample an LLM or request user input.
- As an integrator, I want the SDK to interoperate with MCP reference implementations so that I can mix languages and still have consistent behavior.
- As a security engineer, I want transport and input validation to follow MCP security requirements so that common attack classes (e.g., DNS rebinding) are mitigated.
- As a release engineer, I want deterministic, cross-platform builds and CI so that upgrades are safe and reproducible.

## Functional Requirements

### Scope: Full MCP 2025-11-25

- The SDK MUST implement the MCP protocol revision `2025-11-25`, including:
  - base protocol, lifecycle, and transports
  - all server features (tools, resources, prompts)
  - all client features (roots, sampling, elicitation)
  - utilities (ping, cancellation, progress, pagination, logging, completion, tasks)
  - HTTP authorization per MCP Authorization specification
- The SDK SHOULD support negotiating and interoperating with at least one older MCP revision (e.g., `2024-11-05`) to support legacy servers/clients.

### Normative Conformance (Spec + Schema)

- The SDK MUST conform to the complete MCP 2025-11-25 specification.
- The SDK MUST treat the MCP 2025-11-25 TypeScript schema as the protocol source of truth, and MUST treat the MCP 2025-11-25 JSON Schema as a generated validation artifact.
- If a conflict exists between the TypeScript schema, the JSON Schema, and/or the spec prose, the TypeScript schema is authoritative.
- If this SRS conflicts with the MCP specification or schema, the MCP specification and schema take precedence.
- The SDK MUST vendor (or otherwise pin) the exact MCP 2025-11-25 schema used for validation and MUST expose the schema version/source in its public documentation (e.g., upstream URL + commit hash or release tag).
- This SRS incorporates by reference the pinned local mirror in `.docs/requirements/mcp-spec-2025-11-25/`.
- The full normative MCP requirements set for this project is the union of:
  - this SRS, and
  - all files in `.docs/requirements/mcp-spec-2025-11-25/` listed by `.docs/requirements/mcp-spec-2025-11-25/MANIFEST.md`.

### MCP Spec Mirror Coverage

- This SRS is considered "full-spec mirrored" only if every file listed in `.docs/requirements/mcp-spec-2025-11-25/MANIFEST.md` is present and treated as normative input to implementation and conformance testing.
- Required mirrored spec pages (2025-11-25):
  - `.docs/requirements/mcp-spec-2025-11-25/spec/index.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/changelog.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/architecture/index.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/index.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/ping.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/server/index.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/server/tools.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/server/resources.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/server/prompts.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/logging.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/completion.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/pagination.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/client/roots.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/client/sampling.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/client/elicitation.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/versioning.md`
  - `.docs/requirements/mcp-spec-2025-11-25/spec/schema.md`
  - `.docs/requirements/mcp-spec-2025-11-25/llms.txt`
  - `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts`
  - `.docs/requirements/mcp-spec-2025-11-25/schema/schema.json`

### Protocol Surface (Schema-Defined RPC Methods)

The SDK MUST implement, at minimum, the complete set of RPC method names and notifications defined by the MCP 2025-11-25 schema:

- Requests (either direction, depending on negotiated capabilities):
  - `initialize`
  - `ping`
  - `tools/list`
  - `tools/call`
  - `resources/list`
  - `resources/read`
  - `resources/templates/list`
  - `resources/subscribe`
  - `resources/unsubscribe`
  - `prompts/list`
  - `prompts/get`
  - `logging/setLevel`
  - `completion/complete`
  - `roots/list`
  - `sampling/createMessage`
  - `elicitation/create`
  - `tasks/get`
  - `tasks/result`
  - `tasks/list`
  - `tasks/cancel`

- Notifications (either direction where applicable):
  - `notifications/initialized`
  - `notifications/cancelled`
  - `notifications/progress`
  - `notifications/message`
  - `notifications/tools/list_changed`
  - `notifications/resources/list_changed`
  - `notifications/resources/updated`
  - `notifications/prompts/list_changed`
  - `notifications/roots/list_changed`
  - `notifications/tasks/status`
  - `notifications/elicitation/complete`

### Protocol Versioning and Negotiation

- The SDK MUST implement MCP version identifiers as date strings in the format `YYYY-MM-DD`.
- The SDK MUST support version negotiation during the initialization lifecycle (`initialize` request / response), and MUST expose the negotiated version to the caller.
- The SDK MUST default to the latest MCP version it supports when initiating `initialize`.
- The SDK MUST allow a caller to configure the supported protocol versions (e.g., to pin or restrict versions).
- If version negotiation fails, the SDK MUST surface an actionable error containing the requested version and the supported versions.

### JSON-RPC Message Conformance

- All messages MUST conform to JSON-RPC 2.0.
- Requests MUST have a non-null `id` (string or integer), and IDs MUST be unique per sender within a session.
- Notifications MUST NOT include an `id`.
- Receivers MUST NOT send responses to notifications.
- Responses MUST include either `result` or `error` (not both) and MUST echo the request `id` (except in malformed-request error handling where `id` is unavailable).
- JSON-RPC error `code` values MUST be integers.
- The SDK MUST parse and emit UTF-8 encoded JSON for all transports.

### General Field Semantics

#### `_meta` (Reserved Metadata)

- The SDK MUST support the MCP-reserved `_meta` object on requests, notifications, and results where allowed by the schema.
- The SDK MUST implement MCP `_meta` key-name rules (prefix/name format) and MUST treat MCP-reserved prefixes as reserved (e.g., `io.modelcontextprotocol/`).
- The SDK MUST NOT assume meaning for `_meta` keys other than those explicitly defined by the MCP specification/schema.
- The SDK MUST treat any `_meta` prefix whose second DNS label is `modelcontextprotocol` or `mcp` as MCP-reserved and MUST NOT define extensions using those reserved prefixes.

#### `icons`

- The SDK MUST support the MCP `icons` field where present on MCP entities (e.g., Implementation, Tool, Prompt, Resource, ResourceLink).
- Clients that render icons MUST support at minimum `image/png` and `image/jpeg` (`image/jpg`) MIME types.
- Consumers of icons MUST treat icon metadata and icon bytes as untrusted input.
- Consumers that fetch icons MUST:
  - allow only `https:` and `data:` icon URIs
  - reject icon URIs with unsafe schemes (e.g., `javascript:`, `file:`, `ftp:`, `ws:`) and reject redirects that change scheme or origin
  - fetch icons without credentials (no cookies, no `Authorization` headers, no client credentials)
  - verify icon URIs are same-origin with the MCP server unless explicitly configured to allow trusted origins
  - validate MIME types and file contents before rendering (allowlist; magic-byte detection; reject mismatch)
  - apply size/dimension/frame limits to mitigate resource exhaustion
  - exercise additional hardening/sanitization when supporting `image/svg+xml`

### Error Handling (Protocol-Level)

- The SDK MUST support JSON-RPC error responses and MUST preserve the request `id` when available.
- The SDK MUST support MCP-defined error shapes present in the schema (e.g., `URLElicitationRequiredError` with code `-32042`).
- The SDK SHOULD expose structured error information to callers (code/message/data) without lossy stringification.

### Bidirectional Operation

- The SDK MUST support bidirectional JSON-RPC (both parties can send requests/notifications/responses).
- The SDK MUST provide an API to register handlers for incoming requests per method, and to issue outbound requests with correlation to responses.
- The SDK MUST support concurrent in-flight requests and MUST route responses to the correct awaiting caller.

### Lifecycle Management

- The SDK MUST implement MCP lifecycle phases: Initialization, Operation, Shutdown.
- During operation, both parties MUST respect the negotiated protocol version and MUST only use capabilities that were successfully negotiated.
- The client-side implementation MUST enforce that `initialize` is the first request sent on a session.
- After sending `initialize` and before receiving an `initialize` response, the client SHOULD NOT send any other requests except `ping`.
- After receiving a successful `initialize` response, the client MUST send `notifications/initialized` before proceeding with normal operations.
- The server-side implementation MUST reject or defer requests that are not valid before initialization completes (except where allowed by the spec, e.g., `ping` and server logging).
- The SDK SHOULD implement configurable timeouts for all requests and SHOULD send cancellation notifications on timeout (per MCP cancellation utility).

#### Initialization Requirements

- The SDK MUST implement version negotiation exactly as specified for `initialize` (client proposes a supported version; server responds with either the same version or another supported version; clients that do not support the server-chosen version SHOULD disconnect).
- The SDK MUST implement capability negotiation exactly as specified for the `initialize` request/response.
- The SDK MUST support the `experimental` capability object for both client and server roles and MUST NOT assume semantics for unknown experimental capability keys.
- The SDK MUST support `clientInfo` and `serverInfo` (Implementation metadata) and MUST treat all such metadata as untrusted input.
- The SDK MUST support optional `instructions` in the `initialize` result.
- Before initialization completes:
  - Clients SHOULD NOT send requests other than `ping`.
  - Servers SHOULD NOT send requests other than `ping` and logging (`notifications/message`), and SHOULD NOT send other server features until after receiving `notifications/initialized`.

#### Shutdown Requirements

- For stdio transport, clients SHOULD implement the shutdown sequence described by MCP:
  - close the input stream to the server process
  - wait for server exit (or send `SIGTERM` after a reasonable timeout)
  - send `SIGKILL` after a reasonable timeout following `SIGTERM`

### Transport Support

#### stdio (Required)

- The SDK MUST support the MCP stdio transport.
- Messages MUST be newline-delimited JSON-RPC objects, and MUST NOT contain embedded newlines.
- The server implementation MUST NOT write anything but MCP messages to `stdout`.
- The implementation MAY write diagnostic logs to `stderr`.
- The client MAY capture, forward, or ignore `stderr` output and SHOULD NOT assume `stderr` implies an error condition.
- The client MUST NOT write anything to the server's `stdin` that is not a valid MCP message.
- The SDK MUST provide APIs to:
  - run a server over stdio (read from stdin, write to stdout)
  - run a client that spawns a subprocess server and communicates over stdin/stdout
- The SDK SHOULD provide a safe, cross-platform subprocess abstraction (Windows + POSIX).

#### Streamable HTTP (Required)

- The SDK MUST implement Streamable HTTP per MCP 2025-11-25.
- The SDK MUST support a single MCP endpoint path supporting POST and GET.
- Every JSON-RPC message sent from the client to the server MUST be a new HTTP POST request to the MCP endpoint.
- Client-side HTTP POST MUST send a single JSON-RPC message per request.
- Client-side HTTP POST MUST include `Accept: application/json, text/event-stream`.
- For JSON-RPC notifications/responses sent via HTTP POST:
  - If accepted, the server MUST respond with HTTP 202 and no body.
  - If rejected, the server MUST respond with an HTTP error status (e.g., 400); the body MAY contain a JSON-RPC error response with no `id`.
- For JSON-RPC requests sent via HTTP POST, the server MUST respond with either:
  - `Content-Type: application/json` and a single JSON-RPC response, or
  - `Content-Type: text/event-stream` and an SSE stream containing JSON-RPC messages.
- If SSE is used:
  - The server SHOULD prime reconnection by sending an event ID with empty data.
  - The client MUST respect SSE `retry` fields.
  - If the server attaches SSE event IDs, clients SHOULD attempt resumption by reconnecting via HTTP GET with `Last-Event-ID`.

#### Streamable HTTP SSE Semantics

- For POST-initiated SSE streams, the server MAY send JSON-RPC requests and notifications before sending the JSON-RPC response; these messages SHOULD relate to the originating client request.
- After a server sends an SSE event with an event ID, the server MAY close the connection without terminating the logical SSE stream; clients SHOULD reconnect (poll) and SHOULD respect the SSE `retry` field when present.
- If the server closes the connection without terminating the stream, it SHOULD send an SSE `retry` field before closing.
- Disconnection MUST NOT be interpreted as request cancellation; requestors SHOULD explicitly send `notifications/cancelled` to cancel in-flight requests.
- Clients MAY remain connected to multiple SSE streams simultaneously.
- The server MUST send each JSON-RPC message on exactly one connected stream and MUST NOT broadcast the same message across multiple streams.
- If the server includes SSE event IDs:
  - Event IDs MUST be globally unique across all streams within an MCP session (or across all streams for that client if session management is not in use).
  - Event IDs SHOULD encode sufficient information to identify the originating stream.
  - Resumption MUST be performed via HTTP GET with `Last-Event-ID`, and servers MUST NOT replay messages that were destined for a different stream.
- After the JSON-RPC response has been delivered on a POST-initiated SSE stream, the server SHOULD terminate that SSE stream.

#### Streamable HTTP GET (Server-Initiated Messages)

- The client MUST be able to open an SSE stream via HTTP GET with `Accept: text/event-stream`.
- The server MUST either return `Content-Type: text/event-stream` or HTTP 405 Method Not Allowed.
- If the client supplies `Last-Event-ID`, the server MAY replay/redeliver messages for that stream per spec resumability rules.

#### Streamable HTTP GET Response Constraints

- On a GET-opened SSE stream, the server MAY send JSON-RPC requests and notifications.
- Messages sent on a GET-opened SSE stream SHOULD be unrelated to any concurrently-running client request.
- The server MUST NOT send JSON-RPC responses on a GET-opened SSE stream unless it is resuming/redelivering a stream that was previously associated with a client request.

#### Streamable HTTP Session Management

- If the server issues `MCP-Session-Id` in the InitializeResult HTTP response, the client MUST include `MCP-Session-Id` on all subsequent HTTP requests.
- Session IDs MUST contain visible ASCII characters only (0x21-0x7E) and SHOULD be cryptographically secure.
- If a session ID is required by the server, the server SHOULD respond to non-initialization requests missing `MCP-Session-Id` with HTTP 400.
- If a session expires, the server MUST respond with HTTP 404 to further requests using that session ID.
- Clients receiving HTTP 404 for a session MUST start a new session by re-initializing.
- Clients SHOULD attempt HTTP DELETE with `MCP-Session-Id` to explicitly terminate sessions; servers MAY return 405 if unsupported.

#### Streamable HTTP Protocol Version Header

- For HTTP transports, the client MUST send `MCP-Protocol-Version: <negotiated-version>` on requests after initialization.
- If the server receives an invalid or unsupported `MCP-Protocol-Version`, it MUST respond with HTTP 400.
- For backwards compatibility, if the server does not receive `MCP-Protocol-Version` and has no other way to identify the protocol version, the server SHOULD assume protocol version `2025-03-26`.

#### HTTP Security Requirements

- For HTTP-based transports, servers MUST validate the `Origin` header on all incoming connections to mitigate DNS rebinding attacks; if the `Origin` header is present and invalid, servers MUST respond with HTTP 403 (and MAY include a JSON-RPC error response with no `id`).
- The SDK SHOULD provide an opinionated default for local development: bind to localhost only.

#### Legacy HTTP+SSE Compatibility (Optional)

- The SDK MAY support the deprecated 2024-11-05 HTTP+SSE transport for compatibility.
- If supported, the SDK SHOULD provide client-side fallback detection that mirrors the 2025-11-25 backwards-compatibility guidance:
  - Attempt POST of an `initialize` request to the server URL with `Accept: application/json, text/event-stream`.
  - If POST fails with HTTP 400/404/405, attempt the legacy transport detection GET flow and expect an initial `endpoint` SSE event.

### Core MCP Features (Server)

If implementing an MCP server, the SDK MUST support all server features defined in MCP 2025-11-25:

- Tool discovery and invocation:
  - `tools/list`
  - `tools/call`
  - `notifications/tools/list_changed` when `tools.listChanged` is declared
- Resource discovery and reading:
  - `resources/list`
  - `resources/read`
  - `resources/templates/list`
  - `resources/subscribe`/`resources/unsubscribe` and `notifications/resources/updated` when `resources.subscribe` is declared
  - `notifications/resources/list_changed` when `resources.listChanged` is declared
- Prompt discovery and retrieval:
  - `prompts/list`
  - `prompts/get`
  - `notifications/prompts/list_changed` when `prompts.listChanged` is declared

- Server utilities:
  - `logging/setLevel` (when `logging` capability is declared)
  - `completion/complete` (when `completions` capability is declared)

- Server-to-client requests (when client capabilities indicate support):
  - `roots/list`
  - `sampling/createMessage`
  - `elicitation/create`
  - `tasks/*` requests (when client declares `tasks` capability and relevant sub-capabilities)

The SDK MUST provide a server API to register tools/resources/prompts with:

- stable identifiers (`name` for tools/prompts, `uri` for resources)
- human-readable metadata (`title`, `description`, optional `icons`)
- JSON Schema input validation for tool input schemas and (if present) output schemas

### Core MCP Features (Client)

If implementing an MCP client, the SDK MUST support all client features defined in MCP 2025-11-25:

- `initialize` / `notifications/initialized`
- `tools/list`, `tools/call`
- `resources/list`, `resources/read`, `resources/templates/list`
- `prompts/list`, `prompts/get`

- Handling server-initiated requests (when enabled):
  - `roots/list` requests from server
  - `sampling/createMessage` requests from server
  - `elicitation/create` requests from server
  - handling `notifications/elicitation/complete` (URL-mode completion) and `URLElicitationRequiredError` (error code `-32042`) where applicable

- Client feature capabilities:
  - Roots capability negotiation (`roots.listChanged`) and `notifications/roots/list_changed`
  - Sampling capability negotiation (including `sampling.tools` and optional `sampling.context`)
  - Elicitation capability negotiation (form + url modes)

- Utilities needed to support the above features (pagination cursors, errors, timeouts)

### Utilities

- Ping: the SDK MUST support the MCP ping utility.
  - The ping receiver MUST respond promptly with an empty result (`{}`).
- Logging:
  - The server MUST declare `logging` capability when it supports logging.
  - The server MUST support `logging/setLevel`.
  - The server MUST be able to emit `notifications/message`.
  - Servers MUST NOT include credentials/secrets in log messages.
- Completion:
  - The server MUST declare `completions` capability when it supports completion.
  - The server MUST support `completion/complete`.
  - The SDK MUST support completion reference types `ref/prompt` and `ref/resource`.
  - Clients SHOULD provide prior argument values via `params.context.arguments` when requesting completions for multi-argument prompts/templates.
- Cancellation:
  - The SDK MUST support `notifications/cancelled`.
  - Cancellation notifications MUST only reference request IDs previously issued in the same direction and believed to still be in-progress.
  - Clients MUST NOT attempt to cancel their `initialize` request.
  - For task-augmented requests, cancellation MUST use `tasks/cancel` (not `notifications/cancelled`).
  - Cancellation notifications MUST be best-effort and receivers MAY ignore unknown/already-completed requests; senders SHOULD ignore responses that arrive after cancellation.
  - Receivers SHOULD stop processing cancelled requests, free associated resources, and SHOULD NOT send a response for a cancelled request.
- Progress:
  - The SDK MUST support `notifications/progress` and `progressToken` in `_meta`.
  - Progress tokens MUST be string or integer and MUST be unique across all active requests.
  - Progress notifications MUST only reference tokens for in-progress operations, and the `progress` value MUST increase with each notification.
  - The `progress` and `total` values MAY be floating point.
  - The progress `message` field SHOULD be used for relevant, human-readable progress information when provided.
  - Progress notifications MUST stop after the underlying request (or task) reaches completion/terminal state.
- Pagination:
  - The SDK MUST implement cursor-based pagination patterns consistently across list endpoints.
  - Cursors MUST be treated as opaque and MUST NOT be persisted across sessions.
  - Clients MUST NOT assume a fixed page size.
  - Invalid cursors SHOULD result in JSON-RPC error `-32602` (Invalid params).

### Server Features: Tools (Schema + Spec)

- Servers supporting tools MUST declare the `tools` capability.
- If `tools.listChanged` is declared, the server SHOULD emit `notifications/tools/list_changed` when the list of tools changes.
- `tools/list` MUST support cursor pagination (`params.cursor`, `result.nextCursor`).
- Tool definitions MUST include `name` and `inputSchema` and MAY include `title`, `description`, `icons`, `outputSchema`, and `annotations`.
- `inputSchema` MUST be a valid JSON Schema object (not `null`).
- For tools with no parameters, servers SHOULD use `{"type":"object","additionalProperties":false}` as `inputSchema` (recommended) or `{"type":"object"}`.
- Tool names SHOULD be 1-128 characters, case-sensitive, and limited to ASCII letters/digits plus `_`, `-`, and `.`.
- Tool names SHOULD NOT contain spaces, commas, or other special characters and SHOULD be unique within a server.
- `tools/call` results MUST be returned as `CallToolResult` (including `content` array of `ContentBlock`), and tool execution failures SHOULD be reported as `isError: true` inside `CallToolResult` (not as a protocol-level JSON-RPC error), except for exceptional conditions (unknown tool, malformed params, capability not supported).
- `CallToolResult` MAY include `structuredContent`.
- If a tool definition includes `outputSchema`, servers MUST ensure `structuredContent` conforms to that schema; clients SHOULD validate `structuredContent` against `outputSchema`.
- For backwards compatibility, when returning `structuredContent`, servers SHOULD also include the serialized JSON in a `TextContent` item.
- Clients MUST treat `Tool.annotations` as untrusted hints unless obtained from a trusted server.

### Server Features: Resources (Schema + Spec)

- Servers supporting resources MUST declare the `resources` capability.
- `resources/list` MUST support cursor pagination.
- `resources/read` MUST return `contents` as an array of resource content objects, which may be text (`text`) or binary (`blob` base64), and MUST include `uri` and MAY include `mimeType`.
- Resource and content objects MAY include `annotations` (audience/priority/lastModified) as defined by the spec/schema.
- Resource definitions MAY include `size` (bytes).
- `resources/templates/list` MUST support cursor pagination and MUST return `resourceTemplates` with `uriTemplate` (RFC 6570) and MAY include metadata; servers SHOULD support `completion/complete` to help fill template arguments when `completions` is supported.
- If `resources.subscribe` is declared:
  - the server MUST support `resources/subscribe` and `resources/unsubscribe`.
  - the server MUST be able to emit `notifications/resources/updated` for subscribed resources.
- If `resources.listChanged` is declared, the server SHOULD emit `notifications/resources/list_changed`.
- Custom URI schemes MUST be valid per RFC 3986.
- Servers SHOULD use `https://` resource URIs only when the client can fetch the resource directly without `resources/read`.
- When returning errors for missing resources, servers SHOULD use error code `-32002` (Resource not found).

### Server Features: Prompts (Schema + Spec)

- Servers supporting prompts MUST declare the `prompts` capability.
- `prompts/list` MUST support cursor pagination.
- `prompts/get` MUST accept `arguments` and return `messages` (role + content).
- Prompt definitions MAY include an `arguments` list with per-argument `name`, optional `description`, and optional `required`.
- If `prompts.listChanged` is declared, the server SHOULD emit `notifications/prompts/list_changed`.

### Server Utilities: Logging, Completion, Pagination

- If `logging` is declared, the server MUST support `logging/setLevel` and MAY send `notifications/message` at or above the configured level.
- Log levels MUST follow RFC 5424 syslog levels: `debug`, `info`, `notice`, `warning`, `error`, `critical`, `alert`, `emergency`.
- If `completions` is declared, the server MUST support `completion/complete` and MUST return at most 100 completion values per response.
- Pagination behavior MUST follow MCP cursor semantics for all list endpoints.

### Client Features: Roots

- Clients supporting roots MUST declare the `roots` capability.
- Servers MAY request roots via `roots/list`.
- If `roots.listChanged` is true, clients MUST send `notifications/roots/list_changed` when roots change.
- Root `uri` values MUST be `file://` URIs per the current specification.
- If the client does not support roots, it SHOULD return `-32601` (Method not found) for `roots/list`.

### Client Features: Sampling

- Clients supporting sampling MUST declare the `sampling` capability.
- Servers MUST NOT send tool-enabled sampling fields (`tools`, `toolChoice`) unless the client declares `sampling.tools`; clients MUST return an error if these fields are provided without `sampling.tools`.
- When `toolChoice` is used, the SDK MUST support MCP tool choice modes: `auto` (default), `required`, and `none`.
- Servers SHOULD avoid `includeContext` values other than `none` and MUST NOT use `thisServer`/`allServers` unless the client declares `sampling.context`.
- Sampling messages MUST use roles `user` and `assistant` only.
- The SDK MUST support all schema-defined sampling request/response fields, including (at minimum): `messages`, `modelPreferences`, `systemPrompt`, `maxTokens`, `stopReason`, and `model`.
- For sampling with tools, the SDK MUST enforce MCP tool-use constraints:
  - user messages that contain tool results MUST contain only `tool_result` blocks
  - tool uses MUST be balanced by matching tool results via IDs (`tool_use.id` matched by `tool_result.toolUseId`) before any other message
- Clients SHOULD return a JSON-RPC error with code `-1` when a user rejects a sampling request.

### Client Features: Elicitation

- Clients supporting elicitation MUST declare the `elicitation` capability and MUST support at least one mode (`form` or `url`).
- For backwards compatibility, `elicitation: {}` MUST be treated as form-mode support.
- Servers MUST NOT send elicitation requests with modes not declared by the client.
- Form mode (`mode: "form"` or omitted) MUST use the restricted requested-schema shape (flat object with primitive properties) defined by the spec/schema.
- URL mode (`mode: "url"`) MUST include `url` and `elicitationId` and MUST be used for sensitive interactions; clients MUST follow safe URL handling requirements (no auto-prefetch, no auto-open, explicit user consent, display full URL/domain).
- Servers MUST NOT include sensitive end-user information in URL-mode elicitation URLs and MUST NOT provide pre-authenticated URLs.
- Clients implementing URL-mode elicitation MUST show the full URL before consent and SHOULD highlight the domain; clients SHOULD warn on ambiguous/suspicious URIs (e.g., Punycode).
- Elicitation results MUST use the three-action model: `accept`, `decline`, `cancel`.
- For URL mode elicitation results, `content` MUST be omitted; for form mode, `content` MUST be present only when `action` is `accept`.
- Servers MAY send `notifications/elicitation/complete` for URL-mode elicitations; clients MUST ignore unknown or already-completed elicitation IDs.
- The SDK MUST support `URLElicitationRequiredError` (JSON-RPC error code `-32042`) exactly as specified by the schema.

### Tasks (Experimental in spec; Required for this project)

- The SDK MUST implement task-augmented requests per MCP 2025-11-25 tasks utility.
- The SDK MUST implement capabilities negotiation for `tasks` for both client and server roles.
- The SDK MUST implement `tasks/get`, `tasks/result`, `tasks/list`, and `tasks/cancel`.
- The SDK MUST implement `notifications/tasks/status` handling (send and receive). Sending these notifications MAY be configurable.
- The SDK MUST support associating task-related messages via `_meta.io.modelcontextprotocol/related-task`.
- The SDK MUST follow the tasks utility semantics exactly, including:
  - receiver-generated task IDs (string) with sufficient entropy
  - task statuses: `working`, `input_required`, `completed`, `failed`, `cancelled`
  - tasks MUST begin in `working` status when created
  - valid status transitions and terminal-state immutability
  - task timestamps (`createdAt`, `lastUpdatedAt`) and TTL behavior (including allowing `ttl: null`)
  - `pollInterval` suggestion handling
  - when accepting a task-augmented request, receivers MUST return a `CreateTaskResult` (task data only) as soon as possible after acceptance
  - `tasks/result` blocking semantics until terminal state, and returning exactly what the underlying request would have returned (success or JSON-RPC error)
  - `tasks/cancel` behavior, including rejecting cancellation for already-terminal tasks with `-32602` (Invalid params)
  - `input_required` semantics (receiver moves task to `input_required` when it needs requestor input; requestors encountering `input_required` SHOULD call `tasks/result` to receive input requests)
  - `tasks/list` pagination semantics (`params.cursor` and `result.nextCursor`), and cursor opacity requirements
  - invalid/nonexistent `taskId` in `tasks/get`, `tasks/result`, or `tasks/cancel` MUST return `-32602` (Invalid params)
  - invalid/nonexistent cursor in `tasks/list` MUST return `-32602` (Invalid params)
  - related-task metadata rules (including not relying on related-task metadata for `tasks/get`, `tasks/list`, `tasks/cancel`, and `notifications/tasks/status` where the `taskId` is already in the payload)
  - `tasks/result` responses MUST include `io.modelcontextprotocol/related-task` metadata in `_meta` (as the result payload does not contain the task ID)
  - tool-level task negotiation via `Tool.execution.taskSupport` (`required`/`optional`/`forbidden`) in addition to capabilities negotiation
  - when an authorization context exists, tasks MUST be bound to that context and task operations MUST enforce access control (no cross-user/task leakage)
  - receivers that do not declare task support for a request type MUST ignore task metadata and process that request normally
  - receivers MAY require task augmentation for specific request types, returning `-32600` (Invalid request) for non-task-augmented requests
  - `notifications/tasks/status` is optional; requestors MUST NOT rely on receiving it and SHOULD continue polling via `tasks/get`

### Authorization (Required; Applies to Streamable HTTP)

- The SDK MUST implement the MCP Authorization specification for HTTP-based transports.
- The SDK MUST NOT apply the MCP Authorization specification to stdio transport; credentials for stdio SHOULD be retrieved from the environment.

- Client-side authorization:
  - MUST support attaching `Authorization: Bearer <token>` to every HTTP request.
  - MUST implement the discovery flow using OAuth 2.0 Protected Resource Metadata (RFC 9728), including:
    - parsing `WWW-Authenticate` headers for `resource_metadata`
    - well-known URI probing fallbacks when headers are absent
  - MUST implement authorization server metadata discovery for both RFC8414 and OpenID Connect discovery.
  - MUST implement OAuth 2.1 authorization code flow with PKCE (`S256`) and MUST refuse to proceed if PKCE support cannot be verified via metadata.
  - MUST implement Resource Indicators (RFC 8707) by sending `resource` in both authorization and token requests.
  - MUST implement the spec's scope selection strategy (least privilege; use `WWW-Authenticate scope` when present).
  - SHOULD support Client ID Metadata Documents and SHOULD fall back to Dynamic Client Registration when supported, per spec guidance.
  - MUST securely store tokens and MUST NOT include tokens in URLs/query strings.
  - SHOULD implement step-up authorization behavior for runtime `insufficient_scope` challenges (HTTP 403 + `WWW-Authenticate`) and MUST apply retry limits to avoid loops.
  - SHOULD implement SSRF mitigations when fetching OAuth discovery URLs (e.g., HTTPS enforcement, blocking private IP ranges, redirect validation) per MCP security best practices.

- Server-side authorization:
  - MUST support Protected Resource Metadata publication (RFC 9728) as required by MCP Authorization.
  - MUST return HTTP 401 for missing/invalid tokens and HTTP 403 for insufficient scope, using `WWW-Authenticate` challenges as specified.
  - MUST validate that access tokens are intended for this MCP server (audience binding) and MUST NOT accept token passthrough.
  - MUST NOT use MCP sessions (`MCP-Session-Id`) as authentication.

### Schema Validation

- The SDK MUST use the official MCP JSON Schema for the supported protocol versions as the validation baseline.
- The SDK MUST support JSON Schema 2020-12 for schemas without a `$schema` field.
- The SDK MUST validate schemas according to their declared or default dialect and MUST handle unsupported dialects gracefully with an appropriate error.
- The SDK SHOULD document which JSON Schema dialects it supports beyond the required default.

### Content Types and Constraints

- The SDK MUST support all MCP content types in the schema.
- For general `ContentBlock` (e.g., tool results, embedded resources, prompts), the SDK MUST support: `text`, `image`, `audio`, `resource_link`, `resource` (embedded resource).
- The SDK MUST support optional `annotations` on content blocks where allowed by the schema.
- For sampling message content, the SDK MUST additionally support `tool_use` and `tool_result` blocks where applicable.
- For sampling with tools, the SDK MUST enforce the spec constraints:
  - tool result messages must contain only tool results
  - tool uses must be balanced by matching tool results via IDs

### Interoperability / Conformance Testing

- The SDK MUST include an automated conformance test suite that validates:
  - the pinned spec mirror and pinned schemas are present and are the versions used by the tests
  - message shape against the official schema for the supported protocol version(s)
  - lifecycle ordering rules (initialize/initialized)
  - capability negotiation and enforcement (including `experimental`)
  - transport framing rules (stdio newline delimiting; HTTP semantics)
  - authorization discovery and HTTP auth error semantics (401/403 + WWW-Authenticate)
  - streamable HTTP protocol header and session handling (`MCP-Protocol-Version`, `MCP-Session-Id`)
  - tools: tool schema validation, `CallToolResult` vs protocol errors, and `structuredContent` behavior
  - completions: `ref/prompt` and `ref/resource` reference handling and max-results constraint
  - cancellation/progress: required invariants (initialize not cancellable; monotonic progress; stop on completion)
  - tasks: capability gating, create/get/result/list/cancel semantics, access-control binding when auth context exists
  - sampling/elicitation: schema validation plus content/mode constraints (tool loop balance; URL-mode safety invariants)
- The SDK SHOULD include integration tests against at least one reference client and one reference server (e.g., official Python/TypeScript implementations).

## Non-Functional Requirements

### Platform Support

- The SDK MUST build and run on:
  - Linux (x86_64)
  - macOS (arm64; x86_64 optional)
  - Windows (x86_64)
- The SDK MUST support at minimum one mainstream compiler toolchain per platform:
  - Linux: GCC and/or Clang
  - macOS: Apple Clang
  - Windows: MSVC (Visual Studio)
- The SDK MUST provide a CMake-based build as the primary build system.

### Dependencies and Packaging

- The SDK SHOULD minimize dependencies and MUST document all required/optional third-party dependencies.
- The SDK SHOULD support both static and shared library builds.
- The SDK SHOULD provide installation artifacts usable via `find_package()` (CMake config package) and SHOULD consider distribution via Conan and/or vcpkg.

### Performance and Reliability

- The SDK SHOULD avoid unnecessary heap allocations in hot paths (message parsing, routing) and SHOULD provide backpressure mechanisms for streaming transports.
- The SDK SHOULD provide configurable limits (max message size, max concurrent requests, max streaming duration).
- The SDK SHOULD support graceful shutdown and cleanup of subprocesses and HTTP sessions.

### Security

- The SDK MUST treat all incoming data as untrusted and MUST validate:
  - JSON parsing errors
  - protocol message structure
  - schema conformance where applicable
- The SDK SHOULD provide guardrails against resource exhaustion (oversized JSON, infinite streams, excessive retries).
- For HTTP, the SDK MUST provide the ability to validate `Origin` and SHOULD default to binding localhost for local deployments.

### Documentation

- The SDK MUST include:
  - quickstart for server (stdio + Streamable HTTP + authorization)
  - quickstart for client (stdio + Streamable HTTP + authorization)
  - examples demonstrating tools/resources/prompts/roots/sampling/elicitation/tasks
  - a version support policy describing which MCP revisions are supported and how upgrades are handled

## Out of Scope

- Implementing a full MCP host application UI (the SDK is a library, not a full client app).
- Implementing an OAuth Authorization Server (the SDK may integrate with one but does not need to provide one).
- Non-standard, proprietary protocol extensions unless explicitly requested.

## Acceptance Criteria

- A third-party MCP client can connect to an SDK-built server over Streamable HTTP with authorization enabled and successfully:
  - perform initialize/initialized negotiation
  - list and call tools
  - list and read resources
  - list and get prompts
- An SDK-built client can connect to an MCP server requiring authorization and successfully complete discovery + OAuth 2.1 flow, then invoke server features.
- An SDK-built server can successfully issue `sampling/createMessage` and `elicitation/create` to an SDK-built client and receive valid responses.
- All conformance tests pass on Linux, macOS, and Windows.
