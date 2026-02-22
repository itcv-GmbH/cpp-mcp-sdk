#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/http/sse/event.hpp>

namespace mcp::http::sse::detail
{

inline constexpr unsigned char kVisibleAsciiFirst = 0x21U;
inline constexpr unsigned char kVisibleAsciiLast = 0x7EU;
inline constexpr std::uint64_t kDecimalBase = 10U;
inline constexpr std::size_t kMaxCursorDigits = 20U;

inline auto isVisibleAscii(std::string_view value) -> bool
{
  return !value.empty()
    && std::all_of(value.begin(),
                   value.end(),
                   [](char character) -> bool
                   {
                     const auto byte = static_cast<unsigned char>(character);
                     return byte >= kVisibleAsciiFirst && byte <= kVisibleAsciiLast;
                   });
}

inline auto isValidSseFieldValue(std::string_view value) -> bool
{
  return std::all_of(value.begin(), value.end(), [](char character) -> bool { return character != '\n' && character != '\r'; });
}

inline auto appendDataLines(std::string &encoded, std::string_view data) -> void
{
  if (data.empty())
  {
    encoded += "data:\n";
    return;
  }

  std::size_t begin = 0;
  while (begin <= data.size())
  {
    const std::size_t newline = data.find('\n', begin);
    const std::string_view line = newline == std::string_view::npos ? data.substr(begin) : data.substr(begin, newline - begin);

    encoded += "data:";
    if (!line.empty())
    {
      encoded.push_back(' ');
      encoded.append(line.data(), line.size());
    }
    encoded.push_back('\n');

    if (newline == std::string_view::npos)
    {
      break;
    }

    begin = newline + 1;
  }
}

inline auto parseRetryField(std::string_view value) -> std::optional<std::uint32_t>
{
  if (value.empty())
  {
    return std::nullopt;
  }

  std::uint64_t parsed = 0;
  for (const char character : value)
  {
    if (character < '0' || character > '9')
    {
      return std::nullopt;
    }

    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / kDecimalBase)
    {
      return std::nullopt;
    }

    parsed = (parsed * kDecimalBase) + digit;
    if (parsed > std::numeric_limits<std::uint32_t>::max())
    {
      return std::nullopt;
    }
  }

  return static_cast<std::uint32_t>(parsed);
}

inline auto flushParsedEvent(std::vector<Event> &events, Event &current, bool hasFields) -> void
{
  if (!hasFields)
  {
    return;
  }

  if (!current.data.empty() && current.data.back() == '\n')
  {
    current.data.pop_back();
  }

  events.push_back(std::move(current));
  current = Event {};
}

}  // namespace mcp::http::sse::detail
