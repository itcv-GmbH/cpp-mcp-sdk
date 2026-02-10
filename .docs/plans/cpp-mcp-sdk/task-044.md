# Task ID: [task-044]
# Task Name: [Client Registration Strategies (Pre-reg, CIMD, Dynamic Registration)]

## Context
Implement the MCP Authorization spec's client registration approaches so the OAuth client can obtain or use an appropriate `client_id` and related metadata before starting the authorization code + PKCE flow.

This is client-side only (the SDK does not implement an OAuth Authorization Server).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Authorization: client-side; SHOULD support CIMD and dynamic registration fallback)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md` (Client Registration Approaches; CIMD; Dynamic Registration)

## Output / Definition of Done
* `include/mcp/auth/client_registration.hpp` defines:
  - a registration strategy configuration (pre-registered, CIMD, dynamic)
  - a resolved client identity structure (`client_id`, redirect URIs, auth method)
* Implements client-side selection logic following the authorization spec priority:
  - prefer pre-registered client info when supplied
  - otherwise use CIMD when the authorization server advertises `client_id_metadata_document_supported`
  - otherwise attempt dynamic client registration when `registration_endpoint` is present
  - otherwise surface an actionable error requiring user/host-provided client info
* Unit tests cover:
  - CIMD selected only when supported and config supplies an HTTPS `client_id` URL
  - dynamic registration selected only when supported and enabled
  - correct error when no strategy is feasible

## Step-by-Step Instructions
1. Define a host-provided configuration model:
   - pre-registered: `client_id` (+ optional secret / auth method) and allowed redirect URIs
   - CIMD: an HTTPS `client_id` URL (metadata document URL) and required metadata fields
   - dynamic: enable/disable; optional defaults for `client_name`, redirect URIs, auth method
2. Define the selection algorithm based on discovered authorization server metadata:
   - check `client_id_metadata_document_supported` (CIMD)
   - check `registration_endpoint` (dynamic registration)
3. Implement CIMD support as a *usage mode*:
   - treat `client_id` as an HTTPS URL
   - validate basic requirements (https scheme; includes path component)
   - provide helper to generate/validate the metadata JSON payload (hosting is host responsibility)
4. Implement dynamic client registration (RFC7591) client-side:
   - POST registration request to `registration_endpoint`
   - parse returned client credentials
   - store credentials in token/client storage interface (session-scoped by default)
5. Add tests with canned metadata and mocked registration endpoint responses.

## Verification
* `ctest --test-dir build -R authorization`
