# Task ID: [task-004]
# Task Name: [Unit Tests: Pinned Schema + SDK Version Helpers]

## Context
Lock down the schema pinning surface and version helper APIs with direct unit tests.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Schema pinning requirement; mirror completeness; versioning)
* `include/mcp/schema/pinned_schema.hpp`
* `include/mcp/schema/validator.hpp` (metadata expectations)
* `include/mcp/version.hpp`
* `include/mcp/sdk/version.hpp`
* `tests/schema_pinned_schema_test.cpp` (created in `task-001`)
* `tests/sdk_version_test.cpp` (created in `task-001`)

## Output / Definition of Done
* `tests/schema_pinned_schema_test.cpp` validates:
  - `mcp::schema::detail::pinnedSchemaJson()` is non-empty and parses as JSON
  - pinned schema JSON includes the 2020-12 dialect marker (`draft/2020-12`)
  - `pinnedSchemaSourcePath()` contains the pinned mirror suffix `mcp-spec-2025-11-25/schema/schema.json` (avoid machine-specific absolute path assumptions)
* `tests/sdk_version_test.cpp` validates:
  - `mcp::getLibraryVersion()` returns non-null and equals `mcp::kSdkVersion`
  - `mcp::sdk::get_version()` returns the same version string
  - protocol version constants match `YYYY-MM-DD` shape (use `mcp::transport::http::isValidProtocolVersion`)

## Step-by-Step Instructions
1. In `tests/schema_pinned_schema_test.cpp`, parse `pinnedSchemaJson()` using `jsoncons::json::parse` and assert it is an object.
2. Assert the pinned schema text contains `https://json-schema.org/draft/2020-12/schema` (string contains check).
3. In `tests/sdk_version_test.cpp`, compare `mcp::getLibraryVersion()` to `std::string(mcp::kSdkVersion)`.
4. Validate `mcp::kLatestProtocolVersion`, `mcp::kFallbackProtocolVersion`, and `mcp::kLegacyProtocolVersion` formatting.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_schema_pinned_schema_test --output-on-failure`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_sdk_version_test --output-on-failure`
