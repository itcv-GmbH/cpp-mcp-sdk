#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcp
{
namespace http
{
namespace sse
{

struct EventIdCursor
{
  std::string streamId;
  std::uint64_t cursor = 0;
};

struct Event
{
  std::optional<std::string> event;
  std::optional<std::string> id;
  std::optional<std::uint32_t> retryMilliseconds;
  std::string data;
};

namespace detail
{

inline auto isVisibleAscii(std::string_view value) -> bool
{
  if (value.empty())
  {
    return false;
  }

  for (const char character : value)
  {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21U || byte > 0x7EU)
    {
      return false;
    }
  }

  return true;
}

inline auto isValidSseFieldValue(std::string_view value) -> bool
{
  for (const char character : value)
  {
    if (character == '\n' || character == '\r')
    {
      return false;
    }
  }

  return true;
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

}  // namespace detail

inline auto makeEventId(std::string_view streamId, std::uint64_t cursor) -> std::string
{
  if (!detail::isVisibleAscii(streamId) || streamId.find(':') != std::string_view::npos || cursor == 0)
  {
    return {};
  }

  std::string eventId;
  eventId.reserve(streamId.size() + 1 + 20);
  eventId.append(streamId.data(), streamId.size());
  eventId.push_back(':');
  eventId += std::to_string(cursor);
  return eventId;
}

inline auto parseEventId(std::string_view eventId) -> std::optional<EventIdCursor>
{
  const std::size_t separator = eventId.rfind(':');
  if (separator == std::string_view::npos || separator == 0 || separator + 1 >= eventId.size())
  {
    return std::nullopt;
  }

  const std::string_view streamId = eventId.substr(0, separator);
  const std::string_view cursorText = eventId.substr(separator + 1);
  if (!detail::isVisibleAscii(streamId) || streamId.find(':') != std::string_view::npos)
  {
    return std::nullopt;
  }

  std::uint64_t cursor = 0;
  for (const char character : cursorText)
  {
    if (character < '0' || character > '9')
    {
      return std::nullopt;
    }

    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (cursor > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
    {
      return std::nullopt;
    }

    cursor = (cursor * 10U) + digit;
  }

  if (cursor == 0)
  {
    return std::nullopt;
  }

  return EventIdCursor {std::string(streamId), cursor};
}

inline auto encodeEvent(const Event &event) -> std::string
{
  std::string encoded;

  if (event.event.has_value() && detail::isValidSseFieldValue(*event.event))
  {
    encoded += "event: ";
    encoded += *event.event;
    encoded.push_back('\n');
  }

  if (event.id.has_value() && detail::isValidSseFieldValue(*event.id))
  {
    encoded += "id: ";
    encoded += *event.id;
    encoded.push_back('\n');
  }

  if (event.retryMilliseconds.has_value())
  {
    encoded += "retry: ";
    encoded += std::to_string(*event.retryMilliseconds);
    encoded.push_back('\n');
  }

  detail::appendDataLines(encoded, event.data);
  encoded.push_back('\n');
  return encoded;
}

inline auto encodeEvents(const std::vector<Event> &events) -> std::string
{
  std::string encoded;
  for (const Event &event : events)
  {
    encoded += encodeEvent(event);
  }

  return encoded;
}

}  // namespace sse
}  // namespace http
}  // namespace mcp
