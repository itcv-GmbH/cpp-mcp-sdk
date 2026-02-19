#include <catch2/catch_test_macros.hpp>
#include <mcp/security/crypto_random.hpp>

TEST_CASE("Crypto random bytes generation", "[security][crypto]")
{
  // Placeholder test - will be expanded in future tasks
  std::vector<std::uint8_t> bytes = mcp::security::cryptoRandomBytes(32);
  REQUIRE(bytes.size() == 32);
}
