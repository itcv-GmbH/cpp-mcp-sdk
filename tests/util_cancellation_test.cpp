#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/util/cancellation.hpp>

namespace test_detail
{

auto hasRequestIdValue(const mcp::jsonrpc::RequestId &id, const std::string &expectedValue) -> bool
{
  if (!std::holds_alternative<std::string>(id))
  {
    return false;
  }
  return std::get<std::string>(id) == expectedValue;
}

auto hasRequestIdValue(const mcp::jsonrpc::RequestId &id, std::int64_t expectedValue) -> bool
{
  if (!std::holds_alternative<std::int64_t>(id))
  {
    return false;
  }
  return std::get<std::int64_t>(id) == expectedValue;
}

}  // namespace test_detail

TEST_CASE("jsonToRequestId parses valid JSON values", "[util][cancellation][json]")
{
  SECTION("String request ID")
  {
    const mcp::jsonrpc::JsonValue json = "test-request-id";
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*result, "test-request-id"));
  }

  SECTION("Int64 request ID (positive)")
  {
    const mcp::jsonrpc::JsonValue json = std::int64_t {42};
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*result, std::int64_t {42}));
  }

  SECTION("Int64 request ID (negative)")
  {
    const mcp::jsonrpc::JsonValue json = std::int64_t {-123};
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*result, std::int64_t {-123}));
  }

  SECTION("Int64 request ID (zero)")
  {
    const mcp::jsonrpc::JsonValue json = std::int64_t {0};
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*result, std::int64_t {0}));
  }

  SECTION("UInt64 request ID (within int64 range)")
  {
    const std::uint64_t value = 1000;
    const mcp::jsonrpc::JsonValue json = value;
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*result, static_cast<std::int64_t>(value)));
  }

  SECTION("UInt64 request ID (at int64 max boundary)")
  {
    const std::uint64_t value = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const mcp::jsonrpc::JsonValue json = value;
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*result, std::numeric_limits<std::int64_t>::max()));
  }
}

TEST_CASE("jsonToRequestId rejects invalid JSON values", "[util][cancellation][json]")
{
  SECTION("UInt64 request ID (out of int64 range)")
  {
    const std::uint64_t value = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    const mcp::jsonrpc::JsonValue json = value;
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Boolean value")
  {
    const mcp::jsonrpc::JsonValue json = true;
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Null value")
  {
    const mcp::jsonrpc::JsonValue json = mcp::jsonrpc::JsonValue::null();
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Object value")
  {
    const mcp::jsonrpc::JsonValue json = mcp::jsonrpc::JsonValue::object();
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Array value")
  {
    const mcp::jsonrpc::JsonValue json = mcp::jsonrpc::JsonValue::array();
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Double value")
  {
    const mcp::jsonrpc::JsonValue json = 3.14;
    const std::optional<mcp::jsonrpc::RequestId> result = mcp::util::cancellation::jsonToRequestId(json);

    REQUIRE_FALSE(result.has_value());
  }
}

TEST_CASE("requestIdToJson round-trips correctly", "[util][cancellation][json]")
{
  SECTION("String request ID")
  {
    const mcp::jsonrpc::RequestId id = std::string {"round-trip-string"};
    const mcp::jsonrpc::JsonValue json = mcp::util::cancellation::requestIdToJson(id);

    REQUIRE(json.is_string());
    REQUIRE(json.as<std::string>() == "round-trip-string");

    // Verify round-trip
    const std::optional<mcp::jsonrpc::RequestId> parsed = mcp::util::cancellation::jsonToRequestId(json);
    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*parsed, "round-trip-string"));
  }

  SECTION("Int64 request ID (positive)")
  {
    const mcp::jsonrpc::RequestId id = std::int64_t {999};
    const mcp::jsonrpc::JsonValue json = mcp::util::cancellation::requestIdToJson(id);

    REQUIRE(json.is_int64());
    REQUIRE(json.as<std::int64_t>() == 999);

    // Verify round-trip
    const std::optional<mcp::jsonrpc::RequestId> parsed = mcp::util::cancellation::jsonToRequestId(json);
    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*parsed, std::int64_t {999}));
  }

  SECTION("Int64 request ID (negative)")
  {
    const mcp::jsonrpc::RequestId id = std::int64_t {-456};
    const mcp::jsonrpc::JsonValue json = mcp::util::cancellation::requestIdToJson(id);

    REQUIRE(json.is_int64());
    REQUIRE(json.as<std::int64_t>() == -456);

    // Verify round-trip
    const std::optional<mcp::jsonrpc::RequestId> parsed = mcp::util::cancellation::jsonToRequestId(json);
    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*parsed, std::int64_t {-456}));
  }

  SECTION("Int64 request ID (zero)")
  {
    const mcp::jsonrpc::RequestId id = std::int64_t {0};
    const mcp::jsonrpc::JsonValue json = mcp::util::cancellation::requestIdToJson(id);

    REQUIRE(json.is_int64());
    REQUIRE(json.as<std::int64_t>() == 0);

    // Verify round-trip
    const std::optional<mcp::jsonrpc::RequestId> parsed = mcp::util::cancellation::jsonToRequestId(json);
    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(*parsed, std::int64_t {0}));
  }
}

TEST_CASE("makeCancelledNotification creates valid notification structure", "[util][cancellation][make]")
{
  SECTION("With string request ID and no reason")
  {
    const mcp::jsonrpc::RequestId requestId = std::string {"req-123"};
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(requestId);

    REQUIRE(notification.method == "notifications/cancelled");
    REQUIRE(notification.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *notification.params;
    REQUIRE(params.contains("requestId"));
    REQUIRE(params.at("requestId").as<std::string>() == "req-123");
    REQUIRE_FALSE(params.contains("reason"));
  }

  SECTION("With int64 request ID and no reason")
  {
    const mcp::jsonrpc::RequestId requestId = std::int64_t {456};
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(requestId);

    REQUIRE(notification.method == "notifications/cancelled");
    REQUIRE(notification.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *notification.params;
    REQUIRE(params.contains("requestId"));
    REQUIRE(params.at("requestId").as<std::int64_t>() == 456);
    REQUIRE_FALSE(params.contains("reason"));
  }

  SECTION("With string request ID and reason")
  {
    const mcp::jsonrpc::RequestId requestId = std::string {"req-789"};
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(requestId, "User cancelled operation");

    REQUIRE(notification.method == "notifications/cancelled");
    REQUIRE(notification.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *notification.params;
    REQUIRE(params.contains("requestId"));
    REQUIRE(params.at("requestId").as<std::string>() == "req-789");
    REQUIRE(params.contains("reason"));
    REQUIRE(params.at("reason").as<std::string>() == "User cancelled operation");
  }

  SECTION("With int64 request ID and reason")
  {
    const mcp::jsonrpc::RequestId requestId = std::int64_t {100};
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(requestId, "Timeout");

    REQUIRE(notification.method == "notifications/cancelled");
    REQUIRE(notification.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *notification.params;
    REQUIRE(params.contains("requestId"));
    REQUIRE(params.at("requestId").as<std::int64_t>() == 100);
    REQUIRE(params.contains("reason"));
    REQUIRE(params.at("reason").as<std::string>() == "Timeout");
  }

  SECTION("With empty reason string")
  {
    const mcp::jsonrpc::RequestId requestId = std::string {"req-empty"};
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(requestId, "");

    REQUIRE(notification.params.has_value());
    const mcp::jsonrpc::JsonValue &params = *notification.params;
    REQUIRE(params.contains("reason"));
    REQUIRE(params.at("reason").as<std::string>() == "");
  }
}

TEST_CASE("parseCancelledNotification accepts valid payloads", "[util][cancellation][parse]")
{
  SECTION("Valid with string requestId")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = "cancel-req-1";

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(result->requestId, "cancel-req-1"));
    REQUIRE_FALSE(result->reason.has_value());
  }

  SECTION("Valid with int64 requestId")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = std::int64_t {42};

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(result->requestId, std::int64_t {42}));
    REQUIRE_FALSE(result->reason.has_value());
  }

  SECTION("Valid with requestId and reason")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = "cancel-req-2";
    (*notification.params)["reason"] = "Operation aborted";

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(result.has_value());
    REQUIRE(test_detail::hasRequestIdValue(result->requestId, "cancel-req-2"));
    REQUIRE(result->reason.has_value());
    REQUIRE(*result->reason == "Operation aborted");
  }

  SECTION("Valid with empty reason")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = "cancel-req-3";
    (*notification.params)["reason"] = "";

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(result.has_value());
    REQUIRE(result->reason.has_value());
    REQUIRE(result->reason->empty());
  }
}

TEST_CASE("parseCancelledNotification rejects invalid payloads", "[util][cancellation][parse][negative]")
{
  SECTION("Wrong method name")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/progress";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = "req-1";

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Missing params")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    // No params set

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Params is not an object")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::array();

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Missing requestId field")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["reason"] = "Some reason";

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("requestId is null")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = mcp::jsonrpc::JsonValue::null();

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("requestId is boolean")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = true;

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("requestId is object")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = mcp::jsonrpc::JsonValue::object();

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("requestId is array")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = mcp::jsonrpc::JsonValue::array();

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("reason is not a string (number)")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = "req-1";
    (*notification.params)["reason"] = 123;

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    // Should still parse but reason should not be populated (since it's not a string)
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->reason.has_value());
  }

  SECTION("reason is not a string (boolean)")
  {
    mcp::jsonrpc::Notification notification;
    notification.method = "notifications/cancelled";
    notification.params = mcp::jsonrpc::JsonValue::object();
    (*notification.params)["requestId"] = "req-1";
    (*notification.params)["reason"] = true;

    const std::optional<mcp::util::cancellation::CancelledNotification> result = mcp::util::cancellation::parseCancelledNotification(notification);
    // Should still parse but reason should not be populated
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->reason.has_value());
  }
}

TEST_CASE("End-to-end makeCancelledNotification to parseCancelledNotification", "[util][cancellation][e2e]")
{
  SECTION("Round-trip with string request ID and reason")
  {
    const mcp::jsonrpc::RequestId originalId = std::string {"e2e-request-123"};
    const std::string originalReason = "End-to-end test cancellation";

    // Build notification
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(originalId, originalReason);

    // Parse notification
    const std::optional<mcp::util::cancellation::CancelledNotification> parsed = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(parsed->requestId, "e2e-request-123"));
    REQUIRE(parsed->reason.has_value());
    REQUIRE(*parsed->reason == "End-to-end test cancellation");
  }

  SECTION("Round-trip with int64 request ID and no reason")
  {
    const mcp::jsonrpc::RequestId originalId = std::int64_t {98765};

    // Build notification
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(originalId);

    // Parse notification
    const std::optional<mcp::util::cancellation::CancelledNotification> parsed = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(parsed->requestId, std::int64_t {98765}));
    REQUIRE_FALSE(parsed->reason.has_value());
  }

  SECTION("Round-trip with negative int64 request ID")
  {
    const mcp::jsonrpc::RequestId originalId = std::int64_t {-555};

    // Build notification
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(originalId, "Negative ID test");

    // Parse notification
    const std::optional<mcp::util::cancellation::CancelledNotification> parsed = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(parsed->requestId, std::int64_t {-555}));
    REQUIRE(parsed->reason.has_value());
    REQUIRE(*parsed->reason == "Negative ID test");
  }

  SECTION("Round-trip with zero as request ID")
  {
    const mcp::jsonrpc::RequestId originalId = std::int64_t {0};

    // Build notification
    const mcp::jsonrpc::Notification notification = mcp::util::cancellation::makeCancelledNotification(originalId, "Zero ID test");

    // Parse notification
    const std::optional<mcp::util::cancellation::CancelledNotification> parsed = mcp::util::cancellation::parseCancelledNotification(notification);

    REQUIRE(parsed.has_value());
    REQUIRE(test_detail::hasRequestIdValue(parsed->requestId, std::int64_t {0}));
    REQUIRE(parsed->reason.has_value());
    REQUIRE(*parsed->reason == "Zero ID test");
  }
}

TEST_CASE("isTaskAugmentedRequest identifies task-augmented requests", "[util][cancellation][task]")
{
  SECTION("Request with task object in params")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["task"] = mcp::jsonrpc::JsonValue::object();

    REQUIRE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request with task object containing fields")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["task"] = mcp::jsonrpc::JsonValue::object();
    (*request.params)["task"]["id"] = "task-123";

    REQUIRE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request without params")
  {
    mcp::jsonrpc::Request request;
    request.method = "ping";
    // No params

    REQUIRE_FALSE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request with non-object params")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::array();

    REQUIRE_FALSE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request with object params but no task field")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["name"] = "test-tool";

    REQUIRE_FALSE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request with task field that is not an object (string)")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["task"] = "not-an-object";

    REQUIRE_FALSE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request with task field that is not an object (array)")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["task"] = mcp::jsonrpc::JsonValue::array();

    REQUIRE_FALSE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request with task field that is not an object (null)")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["task"] = mcp::jsonrpc::JsonValue::null();

    REQUIRE_FALSE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }

  SECTION("Request with task field that is not an object (boolean)")
  {
    mcp::jsonrpc::Request request;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["task"] = true;

    REQUIRE_FALSE(mcp::util::cancellation::isTaskAugmentedRequest(request));
  }
}

TEST_CASE("extractTaskId extracts task ID from request params", "[util][cancellation][task]")
{
  SECTION("Valid taskId in params")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = "task-abc-123";

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);

    REQUIRE(result.has_value());
    REQUIRE(*result == "task-abc-123");
  }

  SECTION("Valid taskId with special characters")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = "task_with-special.chars";

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);

    REQUIRE(result.has_value());
    REQUIRE(*result == "task_with-special.chars");
  }
}

TEST_CASE("extractTaskId rejects invalid task IDs", "[util][cancellation][task][negative]")
{
  SECTION("No params")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    // No params

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Params is not an object")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::array();

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Missing taskId field")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["otherField"] = "value";

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("taskId is null")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = mcp::jsonrpc::JsonValue::null();

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("taskId is number")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = 123;

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("taskId is boolean")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = false;

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("taskId is object")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = mcp::jsonrpc::JsonValue::object();

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("taskId is array")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = mcp::jsonrpc::JsonValue::array();

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Empty taskId string")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = "";

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("taskId with whitespace only")
  {
    mcp::jsonrpc::Request request;
    request.method = "tasks/cancel";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["taskId"] = "   ";

    const std::optional<std::string> result = mcp::util::cancellation::extractTaskId(request);
    // Whitespace is valid string content, so it should be accepted
    REQUIRE(result.has_value());
    REQUIRE(*result == "   ");
  }
}

TEST_CASE("extractCreateTaskResultTaskId extracts from success responses", "[util][cancellation][task]")
{
  SECTION("Valid taskId in result.task.taskId")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["task"] = mcp::jsonrpc::JsonValue::object();
    success.result["task"]["taskId"] = "created-task-123";

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE(result.has_value());
    REQUIRE(*result == "created-task-123");
  }

  SECTION("Valid taskId with other fields in result")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["status"] = "pending";
    success.result["task"] = mcp::jsonrpc::JsonValue::object();
    success.result["task"]["taskId"] = "task-xyz";
    success.result["task"]["created"] = true;

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE(result.has_value());
    REQUIRE(*result == "task-xyz");
  }
}

TEST_CASE("extractCreateTaskResultTaskId rejects invalid responses", "[util][cancellation][task][negative]")
{
  SECTION("Error response")
  {
    mcp::jsonrpc::ErrorResponse error;
    error.error.code = -32600;
    error.error.message = "Invalid Request";

    mcp::jsonrpc::Response response = error;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response without result field")
  {
    mcp::jsonrpc::SuccessResponse success;
    // No result set

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response with non-object result")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = "not-an-object";

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response with result missing task field")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["other"] = "value";

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response with task field not an object")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["task"] = "not-an-object";

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response with task object missing taskId")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["task"] = mcp::jsonrpc::JsonValue::object();
    success.result["task"]["status"] = "pending";

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response with taskId not a string (number)")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["task"] = mcp::jsonrpc::JsonValue::object();
    success.result["task"]["taskId"] = 123;

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response with taskId not a string (boolean)")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["task"] = mcp::jsonrpc::JsonValue::object();
    success.result["task"]["taskId"] = true;

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }

  SECTION("Success response with empty taskId")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["task"] = mcp::jsonrpc::JsonValue::object();
    success.result["task"]["taskId"] = "";

    mcp::jsonrpc::Response response = success;
    const std::optional<std::string> result = mcp::util::cancellation::extractCreateTaskResultTaskId(response);

    REQUIRE_FALSE(result.has_value());
  }
}

TEST_CASE("makeTasksCancelRequest constructs correct request", "[util][cancellation][task]")
{
  SECTION("With string request ID")
  {
    const mcp::jsonrpc::RequestId requestId = std::string {"cancel-req-1"};
    const std::string taskId = "task-to-cancel-123";

    const mcp::jsonrpc::Request request = mcp::util::cancellation::makeTasksCancelRequest(requestId, taskId);

    REQUIRE(test_detail::hasRequestIdValue(request.id, "cancel-req-1"));
    REQUIRE(request.method == "tasks/cancel");
    REQUIRE(request.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *request.params;
    REQUIRE(params.contains("taskId"));
    REQUIRE(params.at("taskId").as<std::string>() == "task-to-cancel-123");
  }

  SECTION("With int64 request ID")
  {
    const mcp::jsonrpc::RequestId requestId = std::int64_t {999};
    const std::string taskId = "task-to-cancel-456";

    const mcp::jsonrpc::Request request = mcp::util::cancellation::makeTasksCancelRequest(requestId, taskId);

    REQUIRE(test_detail::hasRequestIdValue(request.id, std::int64_t {999}));
    REQUIRE(request.method == "tasks/cancel");
    REQUIRE(request.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *request.params;
    REQUIRE(params.contains("taskId"));
    REQUIRE(params.at("taskId").as<std::string>() == "task-to-cancel-456");
  }

  SECTION("With negative int64 request ID")
  {
    const mcp::jsonrpc::RequestId requestId = std::int64_t {-1};
    const std::string taskId = "task-to-cancel-789";

    const mcp::jsonrpc::Request request = mcp::util::cancellation::makeTasksCancelRequest(requestId, taskId);

    REQUIRE(test_detail::hasRequestIdValue(request.id, std::int64_t {-1}));
    REQUIRE(request.method == "tasks/cancel");
    REQUIRE(request.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *request.params;
    REQUIRE(params.at("taskId").as<std::string>() == "task-to-cancel-789");
  }

  SECTION("With zero as request ID")
  {
    const mcp::jsonrpc::RequestId requestId = std::int64_t {0};
    const std::string taskId = "task-to-cancel-000";

    const mcp::jsonrpc::Request request = mcp::util::cancellation::makeTasksCancelRequest(requestId, taskId);

    REQUIRE(test_detail::hasRequestIdValue(request.id, std::int64_t {0}));
    REQUIRE(request.method == "tasks/cancel");
    REQUIRE(request.params.has_value());

    const mcp::jsonrpc::JsonValue &params = *request.params;
    REQUIRE(params.at("taskId").as<std::string>() == "task-to-cancel-000");
  }

  SECTION("With empty taskId")
  {
    const mcp::jsonrpc::RequestId requestId = std::string {"req-id"};
    const std::string taskId = "";

    const mcp::jsonrpc::Request request = mcp::util::cancellation::makeTasksCancelRequest(requestId, taskId);

    REQUIRE(request.params.has_value());
    const mcp::jsonrpc::JsonValue &params = *request.params;
    REQUIRE(params.at("taskId").as<std::string>() == "");
  }

  SECTION("JSON-RPC version is set correctly")
  {
    const mcp::jsonrpc::RequestId requestId = std::string {"req-id"};
    const std::string taskId = "task-123";

    const mcp::jsonrpc::Request request = mcp::util::cancellation::makeTasksCancelRequest(requestId, taskId);

    REQUIRE(request.jsonrpc == "2.0");
  }
}

TEST_CASE("Constant values are correct", "[util][cancellation][constants]")
{
  REQUIRE(mcp::util::cancellation::kCancelledNotificationMethod == "notifications/cancelled");
  REQUIRE(mcp::util::cancellation::kTasksCancelMethod == "tasks/cancel");
}
