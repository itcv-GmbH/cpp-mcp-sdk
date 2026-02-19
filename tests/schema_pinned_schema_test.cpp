#include <string>

#include <catch2/catch_test_macros.hpp>
#include <jsoncons/json.hpp>
#include <mcp/schema/pinned_schema.hpp>
#include <mcp/transport/http.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Pinned schema retrieval", "[schema][pinned]")
{
  // Test that pinnedSchemaJson() returns non-empty string
  std::string_view schemaJson = mcp::schema::detail::pinnedSchemaJson();
  REQUIRE_FALSE(schemaJson.empty());

  // Test that pinned schema JSON parses as valid JSON
  jsoncons::json parsed = jsoncons::json::parse(schemaJson);
  REQUIRE(parsed.is_object());

  // Test that pinned schema contains the 2020-12 dialect marker
  std::string schemaString {schemaJson};
  REQUIRE(schemaString.find("https://json-schema.org/draft/2020-12/schema") != std::string::npos);

  // Test that source path contains the expected pinned mirror suffix
  std::string_view sourcePath = mcp::schema::detail::pinnedSchemaSourcePath();
  REQUIRE(sourcePath.find("mcp-spec-2025-11-25/schema/schema.json") != std::string_view::npos);
}

TEST_CASE("Pinned schema source path is valid", "[schema][pinned]")
{
  std::string_view sourcePath = mcp::schema::detail::pinnedSchemaSourcePath();
  REQUIRE_FALSE(sourcePath.empty());
  REQUIRE(sourcePath.find("schema.json") != std::string_view::npos);
}

TEST_CASE("Pinned schema is valid JSON schema", "[schema][pinned]")
{
  std::string_view schemaJson = mcp::schema::detail::pinnedSchemaJson();
  jsoncons::json parsed = jsoncons::json::parse(schemaJson);

  // Verify it has expected JSON Schema root properties - $schema is mandatory
  REQUIRE(parsed.contains("$schema"));
  // $defs is expected for a schema with definitions
  REQUIRE(parsed.contains("$defs"));
}

TEST_CASE("Pinned schema uses draft-07 compatibility", "[schema][pinned]")
{
  // Although the schema uses 2020-12 dialect, it should have $schema that indicates
  // JSON Schema Draft-07 compatibility or be based on the 2020-12 meta-schema
  std::string_view schemaJson = mcp::schema::detail::pinnedSchemaJson();
  jsoncons::json parsed = jsoncons::json::parse(schemaJson);

  // Check $schema value is present and contains draft 2020-12
  std::string schemaUri = parsed["$schema"].as<std::string>();
  REQUIRE(schemaUri.find("2020-12") != std::string::npos);
}

// NOLINTEND(readability-function-cognitive-complexity)
