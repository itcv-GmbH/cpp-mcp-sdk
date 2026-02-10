# Gap Matrix: Existing C/C++ MCP SDKs vs Full MCP 2025-11-25 Requirements

This document compares existing C/C++ MCP libraries against the requirements in `./.docs/requirements/cpp-mcp-sdk.md` (full MCP 2025-11-25, including Streamable HTTP and MCP Authorization).

Legend:

- ✅ Implemented / clearly supported
- ⚠️ Partial / older spec / incompatible details
- ❌ Not implemented
- ? Unknown (not evidenced in public docs/code)

## Candidates Evaluated

- `0xeb/fastmcpp` (C++): https://github.com/0xeb/fastmcpp
- `GopherSecurity/gopher-mcp` (C++): https://github.com/GopherSecurity/gopher-mcp
- `hkr04/cpp-mcp` (C++): https://github.com/hkr04/cpp-mcp
- `Qihoo360/TinyMCP` (C++): https://github.com/Qihoo360/TinyMCP
- `micl2e2/mcpc` (C): https://github.com/micl2e2/mcpc

## Gap Matrix (High-Impact Requirements)

| Requirement Area (MCP 2025-11-25) | fastmcpp | gopher-mcp | cpp-mcp | TinyMCP | mcpc |
| --- | --- | --- | --- | --- | --- |
| Platform build (Linux/macOS/Windows) | ✅ (README: cross-platform) | ✅ (README: cross-platform; CMake) | ✅ (CMake; SSL option) | ✅ (README: Windows/Linux/macOS) | ✅ (README: Linux/Windows/macOS) |
| Protocol revision 2025-11-25 | ⚠️ (mix: mentions 2025-11-25 annotations; transport says 2025-03-26) | ❌ (examples show 2025-06-18; docs show 2024-11-05) | ❌ (README: 2024-11-05) | ❌ (README: 2024-11-05) | ? (README does not claim 2025-11-25; feature list is incomplete) |
| JSON-RPC 2.0 core | ✅ | ✅ | ✅ | ✅ | ✅ |
| stdio transport | ✅ | ✅ | ✅ | ✅ | ⚠️ (server ✅, client WIP) |
| Streamable HTTP transport (single endpoint, POST + GET SSE listen, resumable) | ❌ (GET returns 405; POST-only endpoint) | ❌ (HTTP+SSE two-endpoint design) | ❌ (HTTP+SSE) | ❌ | ❌ |
| Required HTTP headers (`MCP-Protocol-Version`) | ❌ (no evidence of header support) | ❌ (no evidence; uses older transport) | ❌ | ❌ | ❌ |
| Session management (`MCP-Session-Id` behavior) | ⚠️ (has `Mcp-Session-Id`, but not 2025-11-25 Streamable HTTP semantics) | ⚠️ (sessions exist, but transport differs) | ⚠️ (SSE session patterns; not Streamable HTTP) | ? | ❌ |
| OAuth-based MCP Authorization (RFC9728 discovery, WWW-Authenticate challenges, OAuth 2.1 + PKCE) | ❌ (simple static Bearer token; no RFC9728/WWW-Authenticate) | ❌ (no evidence of OAuth flows; README says "authentication middleware") | ❌ (auth token helper, not OAuth) | ❌ | ❌ |
| Tools (`tools/list`, `tools/call`) | ✅ | ✅ | ✅ | ✅ (tools only) | ✅ (server) |
| Resources (`resources/list`, `resources/read`, templates, subscribe) | ✅ | ✅ | ✅ | ❌ (not yet) | ✅ (server) |
| Prompts (`prompts/list`, `prompts/get`) | ✅ | ✅ | ✅ | ❌ (not yet) | ✅ (server) |
| Client feature: Roots (`roots/list`, `notifications/roots/list_changed`) | ⚠️ (roots exist; notification naming appears non-spec in places) | ? | ❌ (no evidence of `roots/list` support) | ❌ | ? |
| Client feature: Sampling (`sampling/createMessage`) | ✅ (explicit support + tests) | ? | ? | ❌ | ❌ |
| Client feature: Elicitation (`elicitation/create`, form + url modes) | ⚠️ (elicitation exists but uses `elicitation/request` in examples/tests; URL mode not evidenced) | ? | ? | ❌ | ❌ |
| Utilities: Ping (`ping`) | ✅ | ? | ✅ | ❌ (README: not yet) | ? |
| Utilities: Cancellation (`notifications/cancelled`) | ✅ | ✅ | ? (documented; no code evidence) | ✅ | ? |
| Utilities: Progress (`notifications/progress`) | ✅ | ✅ | ? (documented; no code evidence) | ✅ | ? |
| Pagination cursors (`nextCursor`) | ✅ | ✅ | ✅ | ✅ | ? |
| Logging (`logging/setLevel`, `notifications/message`) | ? | ? | ? | ❌ (README: not yet) | ? |
| Completion (`completion/complete`) | ? | ? | ? | ❌ (README: not yet) | ✅ (server) |
| Tasks (2025-11-25 tasks utility: `params.task`, `tasks/get|list|result|cancel`) | ⚠️ (has an older "SEP-1686 subset"; semantics differ from 2025-11-25 tasks spec) | ❌ (no evidence) | ❌ | ? | ❌ |

## Evidence Notes (Selected)

### fastmcpp

- Streamable HTTP is not 2025-11-25 compliant: the server registers POST only and returns 405 for GET on the MCP path.
  - Evidence: `src/server/streamable_http_server.cpp` in fastmcpp shows `svr_->Get(...){ res.status = 405; }` and only `svr_->Post(mcp_path_, ...)`.
  - Source: https://raw.githubusercontent.com/0xeb/fastmcpp/main/src/server/streamable_http_server.cpp

- Authorization is a static Bearer token check, not MCP Authorization (OAuth):
  - Evidence: `check_auth()` enforces `Authorization: Bearer <token>` equality and returns 401 with a JSON body; no `WWW-Authenticate` `resource_metadata`, no RFC9728 discovery.
  - Source: https://raw.githubusercontent.com/0xeb/fastmcpp/main/src/server/streamable_http_server.cpp

- Tasks appear implemented as an older subset (mentions "SEP-1686 subset" and uses `_meta` task signaling), which does not match the 2025-11-25 tasks spec that introduces `params.task`.
  - Evidence: fastmcpp tests mention "SEP-1686 subset".
  - Source: https://github.com/0xeb/fastmcpp/blob/main/tests/server/sse_tasks_notifications.cpp

- Elicitation method name mismatch risk: fastmcpp examples/tests use `elicitation/request` rather than spec `elicitation/create`.
  - Source: https://github.com/0xeb/fastmcpp/blob/main/include/fastmcpp/server/context.hpp

### gopher-mcp

- Remote HTTP transport is implemented as classic HTTP+SSE with separate `/rpc` (POST) and `/events` (GET) endpoints; MCP 2025-11-25 requires Streamable HTTP with a single endpoint and GET listening at the same endpoint.
  - Source: https://raw.githubusercontent.com/GopherSecurity/gopher-mcp/main/examples/mcp/README.md
  - Source: https://raw.githubusercontent.com/GopherSecurity/gopher-mcp/main/docs/transport_layer.md

### cpp-mcp

- Repository explicitly targets MCP 2024-11-05.
  - Source: https://raw.githubusercontent.com/hkr04/cpp-mcp/main/README.md

### TinyMCP

- Repository explicitly targets MCP 2024-11-05 and is stdio-only; HTTP+SSE is "not yet".
  - Source (README content on GitHub page): https://github.com/Qihoo360/TinyMCP

### mcpc

- Server-side support is partial and transport support is incomplete (stdio server implemented; HTTP and client WIP).
  - Source: https://raw.githubusercontent.com/micl2e2/mcpc/master/README.md

## Conclusions

- No evaluated C/C++ library appears to implement *full* MCP 2025-11-25, especially:
  - Streamable HTTP (single endpoint with GET SSE listen + resumability)
  - MCP Authorization (OAuth 2.1 + RFC9728 discovery + PKCE + correct `WWW-Authenticate` semantics)
- `0xeb/fastmcpp` is the closest starting point for core protocol breadth (tools/resources/prompts + some client features), but would require substantial work to meet 2025-11-25 Streamable HTTP + OAuth Authorization and to align tasks + elicitation method naming with the current spec.
