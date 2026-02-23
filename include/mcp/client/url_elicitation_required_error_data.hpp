#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <mcp/client/url_elicitation_required_item.hpp>
#include <mcp/sdk/errors.hpp>

namespace mcp::client
{

struct UrlElicitationRequiredErrorData
{
  std::vector<UrlElicitationRequiredItem> elicitations;
};

inline auto parseUrlElicitationRequiredError(const JsonRpcError &error) -> std::optional<UrlElicitationRequiredErrorData>
{
  if (error.code != static_cast<std::int32_t>(JsonRpcErrorCode::kUrlElicitationRequired) || !error.data.has_value() || !error.data->is_object())
  {
    return std::nullopt;
  }

  const auto &data = *error.data;
  if (!data.contains("elicitations") || !data["elicitations"].is_array())
  {
    return std::nullopt;
  }

  UrlElicitationRequiredErrorData parsed;
  parsed.elicitations.reserve(data["elicitations"].size());
  for (const auto &item : data["elicitations"].array_range())
  {
    if (!item.is_object() || !item.contains("mode") || !item["mode"].is_string() || item["mode"].as<std::string>() != "url" || !item.contains("elicitationId")
        || !item["elicitationId"].is_string() || !item.contains("message") || !item["message"].is_string() || !item.contains("url") || !item["url"].is_string())
    {
      return std::nullopt;
    }

    UrlElicitationRequiredItem parsedItem;
    parsedItem.elicitationId = item["elicitationId"].as<std::string>();
    parsedItem.message = item["message"].as<std::string>();
    parsedItem.url = item["url"].as<std::string>();
    parsed.elicitations.push_back(std::move(parsedItem));
  }

  return parsed;
}

}  // namespace mcp::client
