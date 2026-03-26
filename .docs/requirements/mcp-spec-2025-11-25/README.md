# MCP Specification Mirror (2025-11-25)

This directory contains a pinned, local mirror of the MCP specification revision
`2025-11-25`, plus the corresponding upstream schemas.

This mirror exists so other requirements documents (e.g. `.docs/requirements/cpp-mcp-sdk.md`)
can reference a stable, reviewable snapshot.

Sources (canonical):

- Spec pages: https://modelcontextprotocol.io/specification/2025-11-25/
- Versioning: https://modelcontextprotocol.io/specification/versioning
- Schema (TypeScript, source of truth): https://github.com/modelcontextprotocol/specification/blob/main/schema/2025-11-25/schema.ts
- Schema (JSON, generated): https://github.com/modelcontextprotocol/specification/blob/main/schema/2025-11-25/schema.json

Mirror contents:

- `MANIFEST.md` lists mirrored pages and their upstream URLs.
- `llms.txt` is a copy of https://modelcontextprotocol.io/llms.txt used as an index.
- `schema/schema.ts` and `schema/schema.json` are local copies of the upstream schemas.

Completeness:

- This mirror is considered complete when every file listed in `MANIFEST.md` exists in this directory.

Notes:

- These files may include non-ASCII characters because they are direct mirrors of upstream spec content.
- If a conflict exists between a downstream SRS and these mirrored sources, the upstream sources are authoritative.
