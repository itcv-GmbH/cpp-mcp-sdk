# MCP C++ SDK Documentation

Welcome to the documentation for the Model Context Protocol (MCP) C++ SDK. This folder contains detailed guides, API overviews, and policy documents to help you integrate and understand the SDK.

## Getting Started

If you want to get up and running quickly with runnable examples, start here:
- **[Quickstart: MCP Server](quickstart_server.md)** - Run stdio, Streamable HTTP, and dual-transport server examples.
- **[Quickstart: MCP Client](quickstart_client.md)** - Connect a client to an MCP server, handle OAuth flow, and use bidirectional APIs.

## Architecture & API Reference

- **[API Overview](api_overview.md)** - Comprehensive explanation of the SDK's module boundaries, runners, ownership model, and threading behavior.
- **[Dependencies](dependencies.md)** - Lists the required and optional libraries (e.g., Boost, jsoncons, OpenSSL) and their integration strategy via vcpkg.

## Policies & Security

- **[Security Notes and Defaults](security.md)** - Covers origin validation, token storage expectations, SSRF safeguards, URL validation, and runtime limits.
- **[Version Policy](version_policy.md)** - Explains how the SDK tracks MCP protocol revisions (e.g., `2025-11-25`), fallback mechanics, and legacy compatibility.
- **[Schema Pinning](schema_pinning.md)** - Details how the SDK embeds and validates against a pinned copy of the canonical MCP JSON Schema.

## Maintenance

- **[vcpkg Upstream Checklist](vcpkg_upstream_checklist.md)** - Guidelines for maintaining and submitting the vcpkg port upstream.

---

> **Note:** The code examples provided in the `examples/` directory of this repository perfectly mirror the usage described in the quickstarts.
