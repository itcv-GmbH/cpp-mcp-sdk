#include <catch2/catch_test_macros.hpp>
#include <mcp/security/limits.hpp>

TEST_CASE("Security limits defaults", "[security][limits]")
{
  // Placeholder test - will be expanded in future tasks
  mcp::security::RuntimeLimits limits;
  REQUIRE(limits.maxMessageSizeBytes == mcp::security::kDefaultMaxMessageSizeBytes);
}
