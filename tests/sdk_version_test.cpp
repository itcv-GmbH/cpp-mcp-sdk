#include <catch2/catch_test_macros.hpp>
#include <mcp/sdk/version.hpp>

TEST_CASE("SDK version retrieval", "[sdk][version]")
{
  // Placeholder test - will be expanded in future tasks
  const char *version = mcp::sdk::get_version();
  REQUIRE(version != nullptr);
}
