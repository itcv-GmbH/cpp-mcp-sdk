#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace mcp::detail
{

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
// The reinterpret_cast to unsigned char is necessary to avoid undefined behavior
// when calling std::tolower, std::toupper, std::isspace, std::iscntrl, etc.
// These functions have undefined behavior if the argument is not in the range of unsigned char
// or equal to EOF. We explicitly cast to unsigned char before calling these functions.

/// @brief Trims ASCII whitespace from both ends of a string_view.
/// @param value The input string_view to trim.
/// @return A new string_view with leading and trailing ASCII whitespace removed.
/// @note Uses locale-independent ASCII whitespace detection (space, tab, newline, etc.)
inline auto trimAsciiWhitespace(std::string_view value) -> std::string_view
{
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
  {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
  {
    --end;
  }

  return value.substr(begin, end - begin);
}

/// @brief Converts ASCII characters to lowercase.
/// @param text The input string_view to convert.
/// @return A new string with all ASCII letters converted to lowercase.
/// @note Only affects A-Z; non-ASCII bytes are passed through unchanged.
///       Uses locale-independent ASCII conversion.
inline auto toLowerAscii(std::string_view text) -> std::string
{
  std::string normalized;
  normalized.reserve(text.size());

  for (const char character : text)
  {
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }

  return normalized;
}

/// @brief Performs case-insensitive ASCII comparison.
/// @param left Left-hand side string_view.
/// @param right Right-hand side string_view.
/// @return true if the strings are equal ignoring case, false otherwise.
/// @note Only compares A-Z case-insensitively; non-ASCII bytes are compared as-is.
///       Uses locale-independent ASCII conversion.
inline auto equalsIgnoreCaseAscii(std::string_view left, std::string_view right) -> bool
{
  if (left.size() != right.size())
  {
    return false;
  }

  for (std::size_t index = 0; index < left.size(); ++index)
  {
    if (std::tolower(static_cast<unsigned char>(left[index])) != std::tolower(static_cast<unsigned char>(right[index])))
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
inline auto containsAsciiWhitespaceOrControl(std::string_view value) -> bool
{
  return std::any_of(value.begin(),
                     value.end(),
                     [](char character) -> bool
                     {
                       const auto byte = static_cast<unsigned char>(character);
                       return std::isspace(byte) != 0 || std::iscntrl(byte) != 0;
                     });
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

}  // namespace mcp::detail
