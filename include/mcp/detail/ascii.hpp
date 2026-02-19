#pragma once

#include <string>
#include <string_view>

namespace mcp::detail
{

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
// Explicit ASCII range checks avoid locale-dependent behavior of <cctype> functions.
// We use explicit character code ranges instead of std::tolower/isspace/etc.

namespace detail
{

// ASCII whitespace: space (0x20), horizontal tab (0x09), line feed (0x0A),
// vertical tab (0x0B), form feed (0x0C), carriage return (0x0D)
constexpr bool isAsciiWhitespace(char c) noexcept
{
  const auto byte = static_cast<unsigned char>(c);
  return byte == 0x20 || byte == 0x09 || byte == 0x0A || byte == 0x0B || byte == 0x0C || byte == 0x0D;
}

// ASCII control characters: 0x00-0x1F and 0x7F (DEL)
constexpr bool isAsciiControl(char c) noexcept
{
  const auto byte = static_cast<unsigned char>(c);
  return byte <= 0x1F || byte == 0x7F;
}

// ASCII uppercase letter: 'A' (0x41) to 'Z' (0x5A)
constexpr bool isAsciiUpper(char c) noexcept
{
  const auto byte = static_cast<unsigned char>(c);
  return byte >= 0x41 && byte <= 0x5A;
}

// ASCII lowercase letter: 'a' (0x61) to 'z' (0x7A)
constexpr bool isAsciiLower(char c) noexcept
{
  const auto byte = static_cast<unsigned char>(c);
  return byte >= 0x61 && byte <= 0x7A;
}

// Convert ASCII uppercase to lowercase: 'A' -> 'a', others unchanged
constexpr char toAsciiLower(char c) noexcept
{
  if (isAsciiUpper(c))
  {
    return static_cast<char>(static_cast<unsigned char>(c) + 0x20);
  }
  return c;
}

}  // namespace detail

/// @brief Trims ASCII whitespace from both ends of a string_view.
/// @param value The input string_view to trim.
/// @return A new string_view with leading and trailing ASCII whitespace removed.
/// @note Uses explicit ASCII whitespace detection (space, tab, newline, etc.)
///       Does NOT use <cctype> functions to avoid locale-dependent behavior.
inline auto trimAsciiWhitespace(std::string_view value) -> std::string_view
{
  std::size_t begin = 0;
  while (begin < value.size() && detail::isAsciiWhitespace(value[begin]))
  {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && detail::isAsciiWhitespace(value[end - 1]))
  {
    --end;
  }

  return value.substr(begin, end - begin);
}

/// @brief Converts ASCII characters to lowercase.
/// @param text The input string_view to convert.
/// @return A new string with all ASCII letters converted to lowercase.
/// @note Only affects A-Z; non-ASCII bytes (>= 0x80) are passed through unchanged.
///       Does NOT use <cctype> functions to avoid locale-dependent behavior.
inline auto toLowerAscii(std::string_view text) -> std::string
{
  std::string normalized;
  normalized.reserve(text.size());

  for (const char character : text)
  {
    normalized.push_back(detail::toAsciiLower(character));
  }

  return normalized;
}

/// @brief Performs case-insensitive ASCII comparison.
/// @param left Left-hand side string_view.
/// @param right Right-hand side string_view.
/// @return true if the strings are equal ignoring case, false otherwise.
/// @note Only compares A-Z case-insensitively; non-ASCII bytes (>= 0x80)
///       are compared as-is (no case folding). Non-ASCII bytes must match exactly.
///       Does NOT use <cctype> functions to avoid locale-dependent behavior.
inline auto equalsIgnoreCaseAscii(std::string_view left, std::string_view right) -> bool
{
  if (left.size() != right.size())
  {
    return false;
  }

  for (std::size_t index = 0; index < left.size(); ++index)
  {
    const char leftChar = left[index];
    const char rightChar = right[index];

    // Convert both to lowercase using explicit ASCII conversion
    const char leftLower = detail::toAsciiLower(leftChar);
    const char rightLower = detail::toAsciiLower(rightChar);

    if (leftLower != rightLower)
    {
      return false;
    }
  }

  return true;
}

/// @brief Checks if a string contains any ASCII whitespace or control characters.
/// @param value The input string_view to check.
/// @return true if the string contains any ASCII whitespace (space, tab, newline, etc.)
///         or control characters (0x00-0x1F, 0x7F), false otherwise.
/// @note Non-ASCII bytes (0x80-0xFF) are considered safe and do not trigger a positive result.
///       Does NOT use <cctype> functions to avoid locale-dependent behavior.
inline auto containsAsciiWhitespaceOrControl(std::string_view value) -> bool
{
  for (const char character : value)
  {
    if (detail::isAsciiWhitespace(character) || detail::isAsciiControl(character))
    {
      return true;
    }
  }
  return false;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

}  // namespace mcp::detail
