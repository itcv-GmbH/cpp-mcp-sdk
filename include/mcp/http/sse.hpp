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
    if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
    {
      return std::nullopt;
    }

    parsed = (parsed * 10U) + digit;
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

inline auto parseEvents(std::string_view encodedEvents) -> std::vector<Event>
{
  std::vector<Event> events;
  Event current;
  bool hasCurrentFields = false;

  std::size_t cursor = 0;
  while (cursor <= encodedEvents.size())
  {
    const std::size_t lineEnd = encodedEvents.find('\n', cursor);
    std::string_view line = lineEnd == std::string_view::npos ? encodedEvents.substr(cursor) : encodedEvents.substr(cursor, lineEnd - cursor);

    if (!line.empty() && line.back() == '\r')
    {
      line.remove_suffix(1);
    }

    if (lineEnd == std::string_view::npos)
    {
      cursor = encodedEvents.size() + 1;
    }
    else
    {
      cursor = lineEnd + 1;
    }

    if (line.empty())
    {
      detail::flushParsedEvent(events, current, hasCurrentFields);
      hasCurrentFields = false;
      continue;
    }

    if (!line.empty() && line.front() == ':')
    {
      continue;
    }

    const std::size_t separator = line.find(':');
    const std::string_view field = separator == std::string_view::npos ? line : line.substr(0, separator);

    std::string_view value;
    if (separator != std::string_view::npos)
    {
      value = line.substr(separator + 1);
      if (!value.empty() && value.front() == ' ')
      {
        value.remove_prefix(1);
      }
    }

    hasCurrentFields = true;
    if (field == "event")
    {
      current.event = std::string(value);
    }
    else if (field == "id")
    {
      current.id = std::string(value);
    }
    else if (field == "retry")
    {
      current.retryMilliseconds = detail::parseRetryField(value);
    }
    else if (field == "data")
    {
      current.data.append(value.data(), value.size());
      current.data.push_back('\n');
    }
  }

  detail::flushParsedEvent(events, current, hasCurrentFields);
  return events;
}

}  // namespace sse
}  // namespace http
}  // namespace mcp
