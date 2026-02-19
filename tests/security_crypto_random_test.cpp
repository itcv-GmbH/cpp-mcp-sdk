#include <catch2/catch_test_macros.hpp>
#include <mcp/security/crypto_random.hpp>

TEST_CASE("Crypto random bytes generation", "[security][crypto]")
{
  SECTION("Zero length returns empty vector")
  {
    std::vector<std::uint8_t> bytes = mcp::security::cryptoRandomBytes(0);
    REQUIRE(bytes.empty());
    REQUIRE(bytes.size() == 0);
  }

  SECTION("Returns vector of requested size")
  {
    std::vector<std::uint8_t> bytes1 = mcp::security::cryptoRandomBytes(1);
    REQUIRE(bytes1.size() == 1);

    std::vector<std::uint8_t> bytes16 = mcp::security::cryptoRandomBytes(16);
    REQUIRE(bytes16.size() == 16);

    std::vector<std::uint8_t> bytes32 = mcp::security::cryptoRandomBytes(32);
    REQUIRE(bytes32.size() == 32);

    std::vector<std::uint8_t> bytes1024 = mcp::security::cryptoRandomBytes(1024);
    REQUIRE(bytes1024.size() == 1024);
  }

  SECTION("Multiple invocations do not throw")
  {
    REQUIRE_NOTHROW(mcp::security::cryptoRandomBytes(16));
    REQUIRE_NOTHROW(mcp::security::cryptoRandomBytes(16));
    REQUIRE_NOTHROW(mcp::security::cryptoRandomBytes(16));
    REQUIRE_NOTHROW(mcp::security::cryptoRandomBytes(32));
    REQUIRE_NOTHROW(mcp::security::cryptoRandomBytes(32));
    REQUIRE_NOTHROW(mcp::security::cryptoRandomBytes(64));
  }
}
