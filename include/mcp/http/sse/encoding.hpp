#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/http/sse/detail/internal.hpp>
#include <mcp/http/sse/event.hpp>
#include <mcp/http/sse/event_id_cursor.hpp>

namespace mcp::http::sse
{

inline auto makeEventId(std::string_view streamId, std::uint64_t cursor) -> std::string
{
  if (!detail::isVisibleAscii(streamId) || streamId.find(':') != std::string_view::npos || cursor == 0)
  {
    return {};
  }

  std::string eventId;
  eventId.reserve(streamId.size() + 1 + detail::kMaxCursorDigits);
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
    if (cursor > (std::numeric_limits<std::uint64_t>::max() - digit) / detail::kDecimalBase)
    {
      return std::nullopt;
    }

    cursor = (cursor * detail::kDecimalBase) + digit;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

}  // namespace mcp::http::sse
