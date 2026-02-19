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

  SECTION("Non-ASCII bytes pass through unchanged")
  {
    // High bytes (0x80-0xFF) should pass through as-is
    const std::string input = mcp::detail::toLowerAscii("HELLO\xFF\x80");
    REQUIRE(input == "hello\xFF\x80");
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

  SECTION("Non-ASCII bytes compared as-is")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello\xFF", "hello\xFF");
    REQUIRE(result);
  }

  SECTION("Non-ASCII bytes differ")
  {
    const bool result = mcp::detail::equalsIgnoreCaseAscii("hello\xFF", "hello\xFE");
    REQUIRE_FALSE(result);
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

  SECTION("Non-ASCII bytes (0x80-0xFF) are safe")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\xFF\x80\x81");
    REQUIRE_FALSE(result);
  }

  SECTION("High bytes mixed with ASCII")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("hello\xFF world");
    REQUIRE(result);
  }

  SECTION("Only whitespace characters")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl(" \t\n\r");
    REQUIRE(result);
  }

  SECTION("All safe high bytes")
  {
    const bool result = mcp::detail::containsAsciiWhitespaceOrControl("\x80\x81\x82\xFF");
    REQUIRE_FALSE(result);
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
}
