#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/sdk/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

enum class ElicitationAction : std::uint8_t
{
  kAccept,
  kDecline,
  kCancel,
};

struct ElicitationCreateContext
{
  jsonrpc::RequestContext requestContext;
};

struct FormElicitationRequest
{
  std::string message;
  jsonrpc::JsonValue requestedSchema;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct UrlElicitationRequest
{
  std::string elicitationId;
  std::string message;
  std::string url;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct UrlElicitationDisplayInfo
{
  std::string fullUrl;
  std::string domain;
};

struct FormElicitationResult
{
  ElicitationAction action = ElicitationAction::kCancel;
  std::optional<jsonrpc::JsonValue> content;
};

struct UrlElicitationResult
{
  ElicitationAction action = ElicitationAction::kCancel;
};

using FormElicitationHandler = std::function<FormElicitationResult(const ElicitationCreateContext &, const FormElicitationRequest &)>;
using UrlElicitationHandler = std::function<UrlElicitationResult(const ElicitationCreateContext &, const UrlElicitationRequest &)>;
using UrlElicitationCompletionHandler = std::function<void(const ElicitationCreateContext &, std::string_view)>;

struct UrlElicitationRequiredItem
{
  std::string elicitationId;
  std::string message;
  std::string url;
};

struct UrlElicitationRequiredErrorData
{
  std::vector<UrlElicitationRequiredItem> elicitations;
};

inline auto formatUrlForConsent(std::string_view url) -> std::optional<UrlElicitationDisplayInfo>
{
  const std::size_t firstNonWhitespace = url.find_first_not_of(" \t\r\n");
  if (firstNonWhitespace == std::string_view::npos)
  {
    return std::nullopt;
  }

  const std::size_t lastNonWhitespace = url.find_last_not_of(" \t\r\n");
  const std::string_view trimmed = url.substr(firstNonWhitespace, (lastNonWhitespace - firstNonWhitespace) + 1);
  const std::size_t schemeSeparator = trimmed.find("://");
  if (schemeSeparator == std::string_view::npos || schemeSeparator == 0)
  {
    return std::nullopt;
  }

  const std::size_t authorityStart = schemeSeparator + 3;
  std::size_t authorityEnd = trimmed.find_first_of("/?#", authorityStart);
  if (authorityEnd == std::string_view::npos)
  {
    authorityEnd = trimmed.size();
  }

  const std::string_view authority = trimmed.substr(authorityStart, authorityEnd - authorityStart);
  if (authority.empty() || authority.find('@') != std::string_view::npos)
  {
    return std::nullopt;
  }

  std::string_view host = authority;
  if (host.front() == '[')
  {
    const std::size_t closingBracket = host.find(']');
    if (closingBracket == std::string_view::npos || closingBracket <= 1)
    {
      return std::nullopt;
    }

    host = host.substr(1, closingBracket - 1);
  }
  else
  {
    const std::size_t portSeparator = host.rfind(':');
    if (portSeparator != std::string_view::npos)
    {
      host = host.substr(0, portSeparator);
    }
  }

  if (host.empty())
  {
    return std::nullopt;
  }

  UrlElicitationDisplayInfo display;
  display.fullUrl = std::string(trimmed);
  display.domain = std::string(host);
  return display;
}

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

}  // namespace mcp
