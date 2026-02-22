#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/util/cancellation/cancelled_notification.hpp>

namespace mcp::util::cancellation
{

inline constexpr std::string_view kCancelledNotificationMethod = "notifications/cancelled";
inline constexpr std::string_view kTasksCancelMethod = "tasks/cancel";

inline auto requestIdToJson(const jsonrpc::RequestId &requestId) -> jsonrpc::JsonValue
{
  if (std::holds_alternative<std::int64_t>(requestId))
  {
    return {std::get<std::int64_t>(requestId)};
  }

  return {std::get<std::string>(requestId)};
}

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

inline auto parseCancelledNotification(const jsonrpc::Notification &notification) -> std::optional<CancelledNotification>
{
  if (notification.method != kCancelledNotificationMethod || !notification.params.has_value() || !notification.params->is_object())
  {
    return std::nullopt;
  }

  const jsonrpc::JsonValue &params = *notification.params;
  if (!params.contains("requestId"))
  {
    return std::nullopt;
  }

  const std::optional<jsonrpc::RequestId> requestId = jsonToRequestId(params.at("requestId"));
  if (!requestId.has_value())
  {
    return std::nullopt;
  }

  CancelledNotification cancellation;
  cancellation.requestId = *requestId;
  if (params.contains("reason") && params.at("reason").is_string())
  {
    cancellation.reason = params.at("reason").as<std::string>();
  }

  return cancellation;
}

inline auto makeCancelledNotification(const jsonrpc::RequestId &requestId, std::optional<std::string> reason = std::nullopt) -> jsonrpc::Notification
{
  jsonrpc::Notification notification;
  notification.method = std::string(kCancelledNotificationMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  params["requestId"] = requestIdToJson(requestId);
  if (reason.has_value())
  {
    params["reason"] = *reason;
  }

  notification.params = std::move(params);
  return notification;
}

inline auto isTaskAugmentedRequest(const jsonrpc::Request &request) -> bool
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return false;
  }

  const jsonrpc::JsonValue &params = *request.params;
  return params.contains("task") && params.at("task").is_object();
}

inline auto extractTaskId(const jsonrpc::Request &request) -> std::optional<std::string>
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return std::nullopt;
  }

  const jsonrpc::JsonValue &params = *request.params;
  if (!params.contains("taskId") || !params.at("taskId").is_string())
  {
    return std::nullopt;
  }

  std::string taskId = params.at("taskId").as<std::string>();
  if (taskId.empty())
  {
    return std::nullopt;
  }

  return taskId;
}

inline auto extractCreateTaskResultTaskId(const jsonrpc::Response &response) -> std::optional<std::string>
{
  if (!std::holds_alternative<jsonrpc::SuccessResponse>(response))
  {
    return std::nullopt;
  }

  const auto &successResponse = std::get<jsonrpc::SuccessResponse>(response);
  if (!successResponse.result.is_object())
  {
    return std::nullopt;
  }

  if (!successResponse.result.contains("task") || !successResponse.result.at("task").is_object())
  {
    return std::nullopt;
  }

  const jsonrpc::JsonValue &taskObject = successResponse.result.at("task");
  if (!taskObject.contains("taskId") || !taskObject.at("taskId").is_string())
  {
    return std::nullopt;
  }

  std::string taskId = taskObject.at("taskId").as<std::string>();
  if (taskId.empty())
  {
    return std::nullopt;
  }

  return taskId;
}

inline auto makeTasksCancelRequest(jsonrpc::RequestId id, const std::string &taskId) -> jsonrpc::Request
{
  jsonrpc::Request request;
  request.id = std::move(id);
  request.method = std::string(kTasksCancelMethod);
  request.params = jsonrpc::JsonValue::object();
  (*request.params)["taskId"] = taskId;
  return request;
}

}  // namespace mcp::util::cancellation
