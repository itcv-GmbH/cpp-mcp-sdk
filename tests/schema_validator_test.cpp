#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
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

TEST_CASE("Schema validator metadata contains required fields", "[schema][validator][metadata]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::PinnedSchemaMetadata &metadata = validator.metadata();

  REQUIRE_FALSE(metadata.localPath.empty());
  REQUIRE_FALSE(metadata.sha256.empty());
  REQUIRE_FALSE(metadata.upstreamSchemaUrl.empty());
  REQUIRE_FALSE(metadata.upstreamRef.empty());
}

TEST_CASE("validateInstance succeeds for known definition with valid instance", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue validRequest = mcp::schema::JsonValue::parse(R"({
    "jsonrpc":"2.0",
    "id":1,
    "method":"test",
    "params":{}
  })");

  const mcp::schema::ValidationResult result = validator.validateInstance(validRequest, "JSONRPCRequest");
  REQUIRE(result.valid);
  REQUIRE(result.diagnostics.empty());
}

TEST_CASE("validateInstance fails for unknown definition with diagnostic", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue validRequest = mcp::schema::JsonValue::parse(R"({
    "jsonrpc":"2.0",
    "id":1,
    "method":"test"
  })");

  const mcp::schema::ValidationResult result = validator.validateInstance(validRequest, "UnknownDefinitionXYZ");
  REQUIRE_FALSE(result.valid);
  REQUIRE_FALSE(result.diagnostics.empty());
  REQUIRE(test_detail::containsText(result.diagnostics.front().message, "UnknownDefinitionXYZ"));
}

TEST_CASE("validateMcpMethodMessage handles unknown methods gracefully", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();

  SECTION("Unknown method with id validates as generic JSONRPCRequest")
  {
    const mcp::schema::JsonValue unknownMethodRequest = mcp::schema::JsonValue::parse(R"({
      "jsonrpc":"2.0",
      "id":1,
      "method":"unknown/method",
      "params":{}
    })");

    const mcp::schema::ValidationResult result = validator.validateMcpMethodMessage(unknownMethodRequest);
    REQUIRE(result.valid);
    REQUIRE(result.diagnostics.empty());
  }

  SECTION("Unknown method without id validates as JSONRPCNotification")
  {
    const mcp::schema::JsonValue unknownMethodNotification = mcp::schema::JsonValue::parse(R"({
      "jsonrpc":"2.0",
      "method":"unknown/notification",
      "params":{}
    })");

    const mcp::schema::ValidationResult result = validator.validateMcpMethodMessage(unknownMethodNotification);
    REQUIRE(result.valid);
    REQUIRE(result.diagnostics.empty());
  }

  SECTION("Unknown method with invalid type produces diagnostics")
  {
    const mcp::schema::JsonValue invalidUnknownMethod = mcp::schema::JsonValue::parse(R"({
      "jsonrpc":"2.0",
      "method":123
    })");

    const mcp::schema::ValidationResult result = validator.validateMcpMethodMessage(invalidUnknownMethod);
    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.diagnostics.empty());
  }
}

TEST_CASE("formatDiagnostics includes key fields for multiple diagnostics", "[schema][validator]")
{
  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const mcp::schema::JsonValue invalidInstance = mcp::schema::JsonValue::parse(R"({
    "jsonrpc":"2.0",
    "id":"invalid-string-id",
    "method":123,
    "params":"not-an-object"
  })");

  const mcp::schema::ValidationResult result = validator.validateInstance(invalidInstance, "JSONRPCRequest");
  REQUIRE_FALSE(result.valid);
  REQUIRE(result.diagnostics.size() > 1);

  const std::string diagnostics = mcp::schema::formatDiagnostics(result);

  REQUIRE(test_detail::containsText(diagnostics, "instanceLocation"));
  REQUIRE(test_detail::containsText(diagnostics, "evaluationPath"));
  REQUIRE(test_detail::containsText(diagnostics, "schemaLocation"));
  REQUIRE(test_detail::containsText(diagnostics, "error"));

  const std::vector<std::string> diagnosticObjects = [&diagnostics]
  {
    std::vector<std::string> objects;
    std::size_t start = 0;
    std::size_t braceDepth = 0;
    std::size_t objectStart = std::string::npos;
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
      if (diagnostics[i] == '{')
      {
        if (braceDepth == 0)
        {
          objectStart = i;
        }
        ++braceDepth;
      }
      else if (diagnostics[i] == '}')
      {
        --braceDepth;
        if (braceDepth == 0 && objectStart != std::string::npos)
        {
          objects.emplace_back(diagnostics.substr(objectStart, i - objectStart + 1));
          objectStart = std::string::npos;
        }
      }
    }
    return objects;
  }();

  REQUIRE(diagnosticObjects.size() > 1);
}

// NOLINTEND(readability-function-cognitive-complexity)
