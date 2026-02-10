#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/schema/validator.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace test_detail
{

auto containsText(const std::string &text, const std::string &needle) -> bool
{
  return std::search(text.begin(), text.end(), needle.begin(), needle.end()) != text.end();
}

}  // namespace test_detail

TEST_CASE("Schema validator accepts valid initialize request", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue initializeRequest = mcp::schema::JsonValue::parse(R"({
    "jsonrpc":"2.0",
    "id":1,
    "method":"initialize",
    "params":{
      "protocolVersion":"2025-11-25",
      "capabilities":{},
      "clientInfo":{
        "name":"test-client",
        "version":"1.0.0"
      }
    }
  })");

  const mcp::schema::ValidationResult result = validator.validateMcpMethodMessage(initializeRequest);
  REQUIRE(result.valid);
  REQUIRE(result.diagnostics.empty());
}

TEST_CASE("Schema validator reports structured diagnostics for invalid initialize request", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue invalidInitializeRequest = mcp::schema::JsonValue::parse(R"({
    "jsonrpc":"2.0",
    "id":1,
    "method":"initialize",
    "params":{
      "capabilities":{},
      "clientInfo":{
        "name":"test-client",
        "version":"1.0.0"
      }
    }
  })");

  const mcp::schema::ValidationResult result = validator.validateMcpMethodMessage(invalidInitializeRequest);
  REQUIRE_FALSE(result.valid);
  REQUIRE_FALSE(result.diagnostics.empty());
  REQUIRE_FALSE(result.diagnostics.front().message.empty());

  const std::string diagnostics = mcp::schema::formatDiagnostics(result);
  REQUIRE(test_detail::containsText(diagnostics, "instanceLocation"));
  REQUIRE(test_detail::containsText(diagnostics, "error"));
}

TEST_CASE("Tool schema validation defaults to JSON Schema 2020-12", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue toolSchema = mcp::schema::JsonValue::parse(R"({
    "type":"object",
    "properties":{
      "name":{"type":"string"}
    },
    "required":["name"],
    "unevaluatedProperties":false
  })");

  const mcp::schema::ValidationResult result = validator.validateToolSchema(toolSchema, mcp::schema::ToolSchemaKind::kInput);
  REQUIRE(result.valid);
  REQUIRE(result.effectiveDialect == "https://json-schema.org/draft/2020-12/schema");
}

TEST_CASE("Tool schema validation handles unsupported dialect gracefully", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue toolSchema = mcp::schema::JsonValue::parse(R"({
    "$schema":"https://example.com/schema",
    "type":"object"
  })");

  const mcp::schema::ValidationResult result = validator.validateToolSchema(toolSchema, mcp::schema::ToolSchemaKind::kOutput);
  REQUIRE_FALSE(result.valid);
  REQUIRE_FALSE(result.diagnostics.empty());
  REQUIRE(test_detail::containsText(result.diagnostics.front().message, "Unsupported JSON Schema dialect"));
}

TEST_CASE("Tool schema validation rejects invalid schema objects", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue invalidToolSchema = mcp::schema::JsonValue::parse(R"({
    "type":42
  })");

  const mcp::schema::ValidationResult result = validator.validateToolSchema(invalidToolSchema, mcp::schema::ToolSchemaKind::kInput);
  REQUIRE_FALSE(result.valid);
  REQUIRE_FALSE(result.diagnostics.empty());
}

TEST_CASE("JSON-RPC parser applies MCP method-specific schema validation", "[schema][validator][jsonrpc]")
{
  const std::string invalidInitializeRequest = R"({
    "jsonrpc":"2.0",
    "id":1,
    "method":"initialize",
    "params":{
      "capabilities":{},
      "clientInfo":{
        "name":"test-client",
        "version":"1.0.0"
      }
    }
  })";

  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(invalidInitializeRequest), mcp::jsonrpc::MessageValidationError);

  try
  {
    (void)mcp::jsonrpc::parseMessage(invalidInitializeRequest);
    FAIL("Expected MessageValidationError for invalid initialize request");
  }
  catch (const mcp::jsonrpc::MessageValidationError &error)
  {
    REQUIRE(test_detail::containsText(error.what(), "MCP schema validation failed"));
    REQUIRE(test_detail::containsText(error.what(), "instanceLocation"));
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
