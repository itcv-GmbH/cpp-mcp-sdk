# Bugfix: HTTP Server POST Content-Type Validation

## Rationale

`task-015` requires server-side rejection of POST bodies that are missing or using an invalid `Content-Type`. This aligns Streamable HTTP behavior with MCP transport expectations and closes a spec-compliance gap where non-JSON POSTs were previously accepted.

## Change Summary

- Updated `StreamableHttpServer::handlePost` to require a `Content-Type` header on POST requests.
- Added media type normalization for validation: lower-case conversion, ASCII whitespace trim, and `;` parameter stripping.
- Accepted media type is now strictly `application/json` (case-insensitive, optional parameters allowed).
- Missing or non-JSON `Content-Type` now returns HTTP 400 with a JSON-RPC error body that references `Content-Type`.

## Risk Assessment

- Behavioral change for non-compliant clients: requests that previously succeeded with missing or non-JSON `Content-Type` now fail with HTTP 400.
- Low implementation risk: validation is scoped to POST ingress and does not alter JSON-RPC routing, SSE behavior, or authorization flow.
