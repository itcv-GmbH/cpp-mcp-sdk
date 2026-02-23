# MCP Version Policy

This SDK tracks MCP protocol revisions as date strings (`YYYY-MM-DD`).

## Supported revisions in this codebase

Defined in `include/mcp/sdk/version.hpp`:

- `2025-11-25` (`kLatestProtocolVersion`)
- `2025-03-26` (`kFallbackProtocolVersion`)
- `2024-11-05` (`kLegacyProtocolVersion`)

## Negotiation behavior

### Lifecycle (`initialize`)

- Client default advertised support comes from `SessionOptions.supportedProtocolVersions`.
- Default session list is:
  - `2025-11-25`
  - `2024-11-05`
- If the client does not explicitly set `protocolVersion`, the client picks the latest value from its supported list.
- Server selection behavior:
  - If the client-proposed version is supported, server returns it.
  - Otherwise, server returns the latest version from its own supported list.

If the server returns a version the client does not support, initialization fails with a lifecycle error.

### HTTP request validation

`HttpServerOptions.supportedProtocolVersions` defaults to:

- `2025-11-25`
- `2024-11-05`
- `2025-03-26`

If `MCP-Protocol-Version` is absent and no version can be inferred from session state, request validation falls back to `2025-03-26`.

## Upgrade strategy

Use a staged compatibility window when adding or removing protocol revisions.

1. Add new revision constants and validation coverage first.
2. Run both client and server with overlapping supported version lists.
3. Prefer the newest revision by ordering/configuring supported lists appropriately.
4. Monitor negotiated versions in integration tests before removing old revisions.
5. Remove a legacy revision only after all known counterparts negotiate newer versions.

## Recommended configuration patterns

Strict latest-only mode:

```cpp
mcp::lifecycle::session::SessionOptions options;
options.supportedProtocolVersions = {"2025-11-25"};
```

Compatibility mode during migration:

```cpp
mcp::lifecycle::session::SessionOptions options;
options.supportedProtocolVersions = {
  "2025-11-25",
  "2024-11-05",
};
```

For HTTP server request validation, keep `HttpServerOptions.supportedProtocolVersions` aligned with your lifecycle/session policy.
