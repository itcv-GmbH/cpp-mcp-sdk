#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/sdk/version.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("SDK version retrieval", "[sdk][version]")
{
  // Test that getLibraryVersion returns non-null
  const char *libraryVersion = mcp::getLibraryVersion();
  REQUIRE(libraryVersion != nullptr);
  REQUIRE_FALSE(std::string(libraryVersion).empty());

  // Test that getLibraryVersion equals kSdkVersion
  std::string expectedVersion {mcp::kSdkVersion};
  REQUIRE(expectedVersion == libraryVersion);

  // Test that sdk::get_version() returns the same version string
  const char *sdkVersion = mcp::sdk::get_version();
  REQUIRE(sdkVersion != nullptr);
  REQUIRE(std::string(sdkVersion) == expectedVersion);
}

TEST_CASE("SDK version constant consistency", "[sdk][version]")
{
  // Verify get_version() returns the same as getLibraryVersion()
  const char *version1 = mcp::getLibraryVersion();
  const char *version2 = mcp::sdk::get_version();

  REQUIRE(version1 != nullptr);
  REQUIRE(version2 != nullptr);
  REQUIRE(std::string(version1) == std::string(version2));
}

TEST_CASE("Protocol version constants format", "[sdk][version]")
{
  // Validate kLatestProtocolVersion matches YYYY-MM-DD format
  std::string_view latest = mcp::kLatestProtocolVersion;
  REQUIRE(mcp::transport::http::isValidProtocolVersion(latest));

  // Validate kFallbackProtocolVersion matches YYYY-MM-DD format
  std::string_view fallback = mcp::kFallbackProtocolVersion;
  REQUIRE(mcp::transport::http::isValidProtocolVersion(fallback));

  // Validate kLegacyProtocolVersion matches YYYY-MM-DD format
  std::string_view legacy = mcp::kLegacyProtocolVersion;
  REQUIRE(mcp::transport::http::isValidProtocolVersion(legacy));
}

TEST_CASE("Protocol version constants are distinct", "[sdk][version]")
{
  // All protocol version constants should be different
  REQUIRE(mcp::kLatestProtocolVersion != mcp::kFallbackProtocolVersion);
  REQUIRE(mcp::kFallbackProtocolVersion != mcp::kLegacyProtocolVersion);
  REQUIRE(mcp::kLatestProtocolVersion != mcp::kLegacyProtocolVersion);
}

TEST_CASE("JSON-RPC version constant format", "[sdk][version]")
{
  // Verify JSON-RPC version is 2.0
  REQUIRE(mcp::kJsonRpcVersion == "2.0");
}

// NOLINTEND(readability-function-cognitive-complexity)
