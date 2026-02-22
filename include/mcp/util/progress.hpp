#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/util/progress/progress_notification.hpp>

namespace mcp::util::progress
{

inline constexpr std::string_view kProgressNotificationMethod = "notifications/progress";

inline auto jsonToRequestId(const jsonrpc::JsonValue &value) -> std::optional<jsonrpc::RequestId>
{
  if (value.is_string())
  {
    return jsonrpc::RequestId {value.as<std::string>()};
  }

  if (value.is_int64())
  {
    return jsonrpc::RequestId {value.as<std::int64_t>()};
  }

  if (value.is_uint64())
  {
    const std::uint64_t unsignedValue = value.as<std::uint64_t>();
    if (unsignedValue <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
      return jsonrpc::RequestId {static_cast<std::int64_t>(unsignedValue)};
    }
  }

  return std::nullopt;
}

inline auto requestIdToJson(const jsonrpc::RequestId &requestId) -> jsonrpc::JsonValue
{
  if (std::holds_alternative<std::int64_t>(requestId))
  {
    return {std::get<std::int64_t>(requestId)};
  }

  return {std::get<std::string>(requestId)};
}

inline auto jsonToNumber(const jsonrpc::JsonValue &value) -> std::optional<double>
{
  if (value.is_double())
  {
    return value.as<double>();
  }

  if (value.is_int64())
  {
    return static_cast<double>(value.as<std::int64_t>());
  }

  if (value.is_uint64())
  {
    return static_cast<double>(value.as<std::uint64_t>());
  }

  return std::nullopt;
}

inline auto extractProgressToken(const jsonrpc::Request &request) -> std::optional<jsonrpc::RequestId>
{
  if (!request.params.has_value() || !request.params->is_object() || !request.params->contains("_meta"))
  {
    return std::nullopt;
  }

  const jsonrpc::JsonValue &meta = request.params->at("_meta");
  if (!meta.is_object() || !meta.contains("progressToken"))
  {
    return std::nullopt;
  }

  return jsonToRequestId(meta.at("progressToken"));
}

inline auto parseProgressNotification(const jsonrpc::Notification &notification) -> std::optional<ProgressNotification>
{
  if (notification.method != kProgressNotificationMethod || !notification.params.has_value() || !notification.params->is_object())
  {
    return std::nullopt;
  }

  const jsonrpc::JsonValue &params = *notification.params;
  if (!params.contains("progressToken") || !params.contains("progress"))
  {
    return std::nullopt;
  }

  const std::optional<jsonrpc::RequestId> progressToken = jsonToRequestId(params.at("progressToken"));
  const std::optional<double> progress = jsonToNumber(params.at("progress"));
  if (!progressToken.has_value() || !progress.has_value())
  {
    return std::nullopt;
  }

  ProgressNotification parsed;
  parsed.progressToken = *progressToken;
  parsed.progress = *progress;
  if (params.contains("total"))
  {
    parsed.total = jsonToNumber(params.at("total"));
  }

  if (params.contains("message") && params.at("message").is_string())
  {
    parsed.message = params.at("message").as<std::string>();
  }

  parsed.additionalProperties = params;
  parsed.additionalProperties.erase("progressToken");
  parsed.additionalProperties.erase("progress");
  parsed.additionalProperties.erase("total");
  parsed.additionalProperties.erase("message");
  return parsed;
}

inline auto makeProgressNotification(const jsonrpc::RequestId &progressToken,
                                     double progress,
                                     std::optional<double> total = std::nullopt,
                                     std::optional<std::string> message = std::nullopt) -> jsonrpc::Notification
{
  jsonrpc::Notification notification;
  notification.method = std::string(kProgressNotificationMethod);
  notification.params = jsonrpc::JsonValue::object();
  (*notification.params)["progressToken"] = requestIdToJson(progressToken);
  (*notification.params)["progress"] = progress;
  if (total.has_value())
  {
    (*notification.params)["total"] = *total;
  }

  if (message.has_value())
  {
    (*notification.params)["message"] = *message;
  }

  return notification;
}

}  // namespace mcp::util::progress
