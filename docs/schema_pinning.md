# MCP Schema Pinning

This SDK validates MCP messages and tool schemas against a pinned copy of the MCP JSON Schema.

## Pinned Artifact

- Local pinned file: `.docs/requirements/mcp-spec-2025-11-25/schema/schema.json`
- SHA-256: `1ffe4c5577974012f5fa02af14ea88df4b7146679df1abaaad497c8d9230ca8a`

## Upstream Source

- Canonical schema URL: `https://raw.githubusercontent.com/modelcontextprotocol/specification/main/schema/2025-11-25/schema.json`
- Protocol/schema revision tag: `2025-11-25`

## Runtime Usage

- The SDK loads the pinned schema at runtime through `mcp::schema::Validator::loadPinnedMcpSchema()`.
- Method-specific MCP validation uses the pinned schema definitions under `#/$defs`.
- Tool `inputSchema` and `outputSchema` are validated with default dialect `draft 2020-12` when `$schema` is omitted.
