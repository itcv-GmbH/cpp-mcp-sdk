#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <mcp/detail/base64url.hpp>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

TEST_CASE("Base64url encode empty input", "[detail][base64url]")
{
  const std::string encoded = mcp::detail::encodeBase64UrlNoPad("");
  REQUIRE(encoded.empty());
}

TEST_CASE("Base64url decode empty input", "[detail][base64url]")
{
  const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("");
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->empty());
}

TEST_CASE("Base64url roundtrip for arbitrary payloads", "[detail][base64url]")
{
  SECTION("Single byte")
  {
    const std::string original = std::string(1, static_cast<char>(0x00));
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }

  SECTION("Two bytes")
  {
    const std::string original = std::string({static_cast<char>(0xAB), static_cast<char>(0xCD)});
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }

  SECTION("Three bytes")
  {
    const std::string original = std::string({static_cast<char>(0x00), static_cast<char>(0xFF), static_cast<char>(0x42)});
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }

  SECTION("Binary data with NUL bytes")
  {
    std::string original;
    original.push_back(0x00);
    original.push_back(0x01);
    original.push_back(0x02);
    original.push_back(0x00);
    original.push_back(0x03);
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }

  SECTION("Longer payload")
  {
    std::string original;
    for (int i = 0; i < 256; ++i)
    {
      original.push_back(static_cast<char>(i));
    }
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }

  SECTION("All 0xFF bytes")
  {
    std::string original(32, static_cast<char>(0xFF));
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }
}

TEST_CASE("Base64url RFC 4648 test vectors adapted", "[detail][base64url]")
{
  // RFC 4648 test vectors adapted for base64url (no padding)
  // Standard base64 vs base64url differences:
  // Standard: +/ with padding (=)
  // URL-safe: -_ without padding

  SECTION("Empty string")
  {
    REQUIRE(mcp::detail::encodeBase64UrlNoPad("") == "");
  }

  SECTION("'f'")
  {
    REQUIRE(mcp::detail::encodeBase64UrlNoPad("f") == "Zg");
  }

  SECTION("'fo'")
  {
    REQUIRE(mcp::detail::encodeBase64UrlNoPad("fo") == "Zm8");
  }

  SECTION("'foo'")
  {
    REQUIRE(mcp::detail::encodeBase64UrlNoPad("foo") == "Zm9v");
  }

  SECTION("'foob'")
  {
    REQUIRE(mcp::detail::encodeBase64UrlNoPad("foob") == "Zm9vYg");
  }

  SECTION("'fooba'")
  {
    REQUIRE(mcp::detail::encodeBase64UrlNoPad("fooba") == "Zm9vYmE");
  }

  SECTION("'foobar'")
  {
    REQUIRE(mcp::detail::encodeBase64UrlNoPad("foobar") == "Zm9vYmFy");
  }

  // Decode verification
  SECTION("Decode 'f' vector")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zg");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "f");
  }

  SECTION("Decode 'fo' vector")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm8");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "fo");
  }

  SECTION("Decode 'foo' vector")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "foo");
  }

  SECTION("Decode 'foob' vector")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9vYg");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "foob");
  }

  SECTION("Decode 'fooba' vector")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9vYmE");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "fooba");
  }

  SECTION("Decode 'foobar' vector")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9vYmFy");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "foobar");
  }
}

TEST_CASE("Base64url URL-safe alphabet specific", "[detail][base64url]")
{
  // Test that URL-safe characters are used correctly
  // Binary data that would produce + or / in standard base64 should produce - or _

  SECTION("Byte value that encodes to '-' (62 in base64)")
  {
    // 0xFB = 11111011 -> should produce '-' (62 in base64url)
    std::string input(1, static_cast<char>(0xFB));
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(input);
    REQUIRE(encoded.find('+') == std::string::npos);
    REQUIRE(encoded.find('/') == std::string::npos);
    REQUIRE(encoded.find('-') != std::string::npos);
  }

  SECTION("Byte value that encodes to '_' (63 in base64)")
  {
    // 0xFF = 11111111 -> should produce '_' (63 in base64url)
    std::string input(1, static_cast<char>(0xFF));
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(input);
    REQUIRE(encoded.find('+') == std::string::npos);
    REQUIRE(encoded.find('/') == std::string::npos);
    REQUIRE(encoded.find('_') != std::string::npos);
  }

  SECTION("Decode accepts '-' correctly")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("-_8");
    REQUIRE(decoded.has_value());
  }

  SECTION("Decode accepts '_' correctly")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("__8");
    REQUIRE(decoded.has_value());
  }
}

TEST_CASE("Base64url invalid input handling", "[detail][base64url]")
{
  SECTION("Reject standard base64 characters '+'")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v+ig");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject standard base64 characters '/'")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v/ig");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject padding '='")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v=");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject remainder length 1 (invalid quantum)")
  {
    // A single character is invalid base64 quantum
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Z");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject whitespace")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v Yg");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject tab character")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v\tYg");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject newline")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v\nYg");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject carriage return")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v\rYg");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject null byte in input")
  {
    std::string input = "Zm9v";
    input.push_back('\0');
    input += "Yg";
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(input);
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject invalid characters")
  {
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v!Yg");
    REQUIRE_FALSE(decoded.has_value());
  }

  SECTION("Reject non-ASCII characters")
  {
    std::string input = "Zm9v";
    input.push_back(static_cast<char>(0xC0));  // UTF-8 continuation byte
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(input);
    REQUIRE_FALSE(decoded.has_value());
  }
}

TEST_CASE("Base64url remainder handling", "[detail][base64url]")
{
  SECTION("Remainder 0 (exact multiple of 4 chars)")
  {
    // "foo" = 3 bytes -> 4 chars (no padding in base64url)
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm9v");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "foo");
  }

  SECTION("Remainder 2 (1 extra byte)")
  {
    // "f" = 1 byte -> 2 chars
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zg");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "f");
  }

  SECTION("Remainder 3 (2 extra bytes)")
  {
    // "fo" = 2 bytes -> 3 chars
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Zm8");
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "fo");
  }

  SECTION("Remainder 1 (invalid)")
  {
    // Any string with length % 4 == 1 is invalid
    // Single character is remainder 1
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad("Z");
    REQUIRE_FALSE(decoded.has_value());
    // 5 characters is also remainder 1
    const std::optional<std::string> decoded2 = mcp::detail::decodeBase64UrlNoPad("Zm9vY");
    REQUIRE_FALSE(decoded2.has_value());
  }
}

TEST_CASE("Base64url edge cases", "[detail][base64url]")
{
  SECTION("All zeros")
  {
    std::string original(16, '\0');
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }

  SECTION("Large payload")
  {
    std::string original(1000, 'A');
    const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
    const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
  }

  SECTION("One byte at a time")
  {
    for (int i = 0; i < 256; ++i)
    {
      const std::string original(1, static_cast<char>(i));
      const std::string encoded = mcp::detail::encodeBase64UrlNoPad(original);
      const std::optional<std::string> decoded = mcp::detail::decodeBase64UrlNoPad(encoded);
      REQUIRE(decoded.has_value());
      REQUIRE(*decoded == original);
    }
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
