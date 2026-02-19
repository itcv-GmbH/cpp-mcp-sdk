#include <cstdint>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <mcp/detail/ascii.hpp>

namespace
{

// Helper to create string_view from raw string literal
constexpr auto sv = [](const char *str) -> std::string_view { return str; };

}  // namespace

TEST_CASE("trimAsciiWhitespace handles empty and edge cases", "[detail][ascii][trim]")
{
  SECTION("Empty string")
  {
    const std::string_view input = "";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result.empty());
  }

  SECTION("All whitespace")
  {
    const std::string_view input = "   \t\n\r  ";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result.empty());
  }

  SECTION("No whitespace")
  {
    const std::string_view input = "hello";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result == "hello");
  }

  SECTION("Leading whitespace only")
  {
    const std::string_view input = "   hello";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result == "hello");
  }

  SECTION("Trailing whitespace only")
  {
    const std::string_view input = "hello   ";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result == "hello");
  }

  SECTION("Both leading and trailing whitespace")
  {
    const std::string_view input = " \t\nhello\r\n ";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result == "hello");
  }

  SECTION("Single character whitespace")
  {
    const std::string_view input = " ";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result.empty());
  }

  SECTION("Multiple whitespace types in sequence")
  {
    const std::string_view input = " \t\r\n hello \t\r\n";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result == "hello");
  }

  SECTION("Non-ASCII whitespace NOT trimmed (bytes >= 0x80)")
  {
    // Non-ASCII bytes should NOT be treated as whitespace
    const std::string_view input = "\x80\x81\x82hello\xFF\xFE\xFD";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result == input);
  }

  SECTION("Only non-ASCII bytes - no trimming")
  {
    const std::string_view input = "\x80\x81\x82";
    const std::string_view result = mcp::detail::trimAsciiWhitespace(input);
    REQUIRE(result == input);
  }
}

TEST_CASE("toLowerAscii converts correctly", "[detail][ascii][tolower]")
{
  SECTION("Empty string")
  {
    const std::string input = mcp::detail::toLowerAscii("");
    REQUIRE(input.empty());
  }

  SECTION("All uppercase")
  {
    const std::string input = mcp::detail::toLowerAscii("HELLO");
    REQUIRE(input == "hello");
  }

  SECTION("All lowercase")
  {
    const std::string input = mcp::detail::toLowerAscii("hello");
    REQUIRE(input == "hello");
  }

  SECTION("Mixed case")
  {
    const std::string input = mcp::detail::toLowerAscii("HeLLo WoRLD");
    REQUIRE(input == "hello world");
  }

  SECTION("Non-ASCII bytes >= 0x80 pass through unchanged")
  {
    // High bytes (0x80-0xFF) should pass through as-is, NOT converted
    const std::string input = mcp::detail::toLowerAscii("HELLO\xFF\x80\xC0\xE0\xF0");
    REQUIRE(input == "hello\xFF\x80\xC0\xE0\xF0");
  }

  SECTION("Non-ASCII bytes not affected by case folding")
  {
    // Bytes >= 0x80 must NOT be involved in case conversion
    // Verify each high byte is preserved exactly
    const char highBytes[] = "\x80\x81\xFF\xC0\xE0\xF0\xFC";
    std::string input = "ABC";
    input += std::string(highBytes, sizeof(highBytes) - 1);
    input += "XYZ";

    const std::string result = mcp::detail::toLowerAscii(input);

    // Check ASCII part converted
    REQUIRE(result.substr(0, 3) == "abc");
    // Check high bytes unchanged
    REQUIRE(result.substr(3, sizeof(highBytes) - 1) == std::string(highBytes, sizeof(highBytes) - 1));
    // Check trailing ASCII
    REQUIRE(result.substr(3 + sizeof(highBytes) - 1) == "xyz");
  }

  SECTION("Numbers and symbols unchanged")
  {
    const std::string input = mcp::detail::toLowerAscii("123!@#");
    REQUIRE(input == "123!@#");
  }

  SECTION("Header name style (Content-Type)")
  {
    const std::string input = mcp::detail::toLowerAscii("Content-Type");
    REQUIRE(input == "content-type");
  }

  SECTION("Extended ASCII (0x80-0xFF) not converted")
  {
    // Verify bytes in 0x80-0xFF range are NOT treated as letters
    const std::string input = "\xC0\xC1\xD0\xD1\xE0\xE1\xF0\xF1";  // Some bytes that could be letters in ISO-8859-1
    const std::string result = mcp::detail::toLowerAscii(input);
    // These should NOT be converted to lowercase (they're not ASCII A-Z)
    REQUIRE(result == input);
  }
}

TEST_CASE("equalsIgnoreCaseAscii compares correctly", "[detail][ascii][equals]")
{
  SECTION("Empty strings")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("", "");
    REQUIRE(result);
  }

  SECTION("Equal strings")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello", "hello");
    REQUIRE(result);
  }

  SECTION("Equal strings different case")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("HELLO", "hello");
    REQUIRE(result);
  }

  SECTION("Equal strings mixed case")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("HeLLo", "hEllO");
    REQUIRE(result);
  }

  SECTION("Different lengths")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello", "hell");
    REQUIRE_FALSE(result);
  }

  SECTION("Same length different content")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello", "world");
    REQUIRE_FALSE(result);
  }

  SECTION("Case difference only")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello", "HELLO");
    REQUIRE(result);
  }

  SECTION("Header name matching (Content-Type)")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("content-type", "Content-Type");
    REQUIRE(result);
  }

  SECTION("Non-ASCII bytes >= 0x80 compared as-is (byte comparison)")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello\xFF", "hello\xFF");
    REQUIRE(result);
  }

  SECTION("Non-ASCII bytes differ - byte-level comparison")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello\xFF", "hello\xFE");
    REQUIRE_FALSE(result);
  }

  SECTION("Non-ASCII bytes NOT involved in case folding")
  {
    // This is critical: bytes >= 0x80 should NOT participate in case-insensitive comparison
    // They should be compared as raw bytes
    const bool result1 = mcp::detail::equalsIgnoreCaseAscii("ABC\x80", "abc\x80");
    REQUIRE(result1);

    // Different high bytes should not match
    const bool result2 = mcp::detail::equalsIgnoreCaseAscii("ABC\x80", "abc\x81");
    REQUIRE_FALSE(result2);
  }

  SECTION("High bytes (>= 0x80) must match exactly for equality")
  {
    // Even though comparison is case-insensitive for ASCII, high bytes must EXACTLY match
    const bool result = mcp::detail::equalsIgnoreCaseAscii("\xFF\xFE\x80", "\xff\xfe\x80");
    REQUIRE(result);
  }
}

TEST_CASE("containsAsciiWhitespaceOrControl detects correctly", "[detail][ascii][contains]")
{
  SECTION("Empty string")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("");
    REQUIRE_FALSE(result);
  }

  SECTION("No whitespace or control")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello");
    REQUIRE_FALSE(result);
  }

  SECTION("Contains space")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello world");
    REQUIRE(result);
  }

  SECTION("Contains tab")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\tworld");
    REQUIRE(result);
  }

  SECTION("Contains newline")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\nworld");
    REQUIRE(result);
  }

  SECTION("Contains carriage return")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\rworld");
    REQUIRE(result);
  }

  SECTION("Contains control character (0x01)")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\x01world");
    REQUIRE(result);
  }

  SECTION("Contains DEL character (0x7F)")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\x7Fworld");
    REQUIRE(result);
  }

  SECTION("Non-ASCII bytes (0x80-0xFF) are safe - NOT detected as whitespace/control")
  {
    // High bytes (0x80-0xFF) should NOT trigger detection
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\xFF\x80\xC0\xE0");
    REQUIRE_FALSE(result);
  }

  SECTION("High bytes mixed with ASCII")
  {
    // High bytes alone should not trigger
    const bool result1 = mcp::detail::containsAsciiWhitespaceOrControl("hello\xFF world");
    REQUIRE(result1);  // Contains space

    // Without space, should be false
    const bool result2 = mcp::detail::containsAsciiWhitespaceOrControl("hello\xFFworld");
    REQUIRE_FALSE(result2);  // No ASCII whitespace/control
  }

  SECTION("Only whitespace characters")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl(" \t\n\r");
    REQUIRE(result);
  }

  SECTION("All safe high bytes - no detection")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("\x80\x81\x82\xFF");
    REQUIRE_FALSE(result);
  }

  SECTION("All control characters detected")
  {
    // ASCII control: 0x00-0x1F
    // Use char array with explicit length to handle embedded nulls
    const char controlChars[] = "\x00\x01\x1F";
    const std::string_view test(controlChars, 3);
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl(test);
    REQUIRE(result);
  }

  SECTION("DEL (0x7F) is control")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("\x7F");
    REQUIRE(result);
  }
}

TEST_CASE("Edge cases for all functions", "[detail][ascii][edge]")
{
  SECTION("Very long string with whitespace")
  {
    const std::string longStr(1000, ' ');
    const std::string_view result = mcp::detail::trimAsciiWhitespace(longStr);
    REQUIRE(result.empty());
  }

  SECTION("Single character content")
  {
    REQUIRE(mcp::detail::trimAsciiWhitespace("a") == "a");
    REQUIRE(mcp::detail::toLowerAscii("A") == "a");
    REQUIRE(mcp::detail::equalsIgnoreCaseAscii("a", "A"));
    REQUIRE_FALSE(mcp::detail::containsAsciiWhitespaceOrControl("a"));
  }

  SECTION("Mixed ASCII and non-ASCII in comparison")
  {
    // Non-ASCII bytes should be compared as bytes
    REQUIRE(mcp::detail::equalsIgnoreCaseAscii("abc\xFF", "ABC\xFF"));
    REQUIRE_FALSE(mcp::detail::equalsIgnoreCaseAscii("abc\xFF", "ABC\xFE"));
  }

  SECTION("URL-like strings")
  {
    // Common in HTTP header handling
    REQUIRE(mcp::detail::trimAsciiWhitespace("  https://example.com  ") == "https://example.com");
    REQUIRE(mcp::detail::toLowerAscii("HTTPS://EXAMPLE.COM") == "https://example.com");
    REQUIRE(mcp::detail::equalsIgnoreCaseAscii("https://example.com", "HTTPS://EXAMPLE.COM"));
  }

  SECTION("UTF-8 like strings with high bytes")
  {
    // Simulate UTF-8 multi-byte sequences - these should be preserved
    const std::string utf8Text = "Hello\xC3\xA9World";  // "é" in UTF-8
    REQUIRE(mcp::detail::toLowerAscii(utf8Text) == "hello\xC3\xA9world");
    REQUIRE(mcp::detail::equalsIgnoreCaseAscii("HELLO\xC3\xA9", "hello\xC3\xA9"));
  }

  SECTION("Binary-like data")
  {
    // Simulate binary data with high bytes - use char array with explicit length
    const char binaryData[] = "\x00\x01\x02\x7F\x80\x81\xFF";
    const std::string binary(binaryData, sizeof(binaryData) - 1);
    const std::string_view trimmed = mcp::detail::trimAsciiWhitespace(binary);
    // Only ASCII whitespace should be trimmed, not high bytes
    REQUIRE(trimmed == binary);
  }
}
