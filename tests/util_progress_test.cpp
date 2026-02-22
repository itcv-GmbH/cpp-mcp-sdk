#include <cstdint>
#include <limits>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/util/progress.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace test_detail
{

auto makeNotification(const std::string &method, const mcp::jsonrpc::JsonValue &params) -> mcp::jsonrpc::Notification
{
  mcp::jsonrpc::Notification notification;
  notification.method = method;
  notification.params = params;
  return notification;
}

}  // namespace test_detail

TEST_CASE("extractProgressToken handles missing params", "[util][progress]")
{
  mcp::jsonrpc::Request request;
  request.method = "tools/call";
  // No params set
  request.params = std::nullopt;

  const auto result = mcp::util::progress::extractProgressToken(request);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("extractProgressToken handles non-object params", "[util][progress]")
{
  mcp::jsonrpc::Request request;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::array();  // Array instead of object

  const auto result = mcp::util::progress::extractProgressToken(request);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("extractProgressToken handles missing _meta", "[util][progress]")
{
  mcp::jsonrpc::Request request;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["otherKey"] = "value";

  const auto result = mcp::util::progress::extractProgressToken(request);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("extractProgressToken handles non-object _meta", "[util][progress]")
{
  mcp::jsonrpc::Request request;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"] = "not an object";

  const auto result = mcp::util::progress::extractProgressToken(request);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("extractProgressToken handles missing progressToken in _meta", "[util][progress]")
{
  mcp::jsonrpc::Request request;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"]["otherKey"] = "value";

  const auto result = mcp::util::progress::extractProgressToken(request);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("extractProgressToken extracts string progressToken", "[util][progress]")
{
  mcp::jsonrpc::Request request;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"]["progressToken"] = "token-123";

  const auto result = mcp::util::progress::extractProgressToken(request);
  REQUIRE(result.has_value());
  REQUIRE(std::holds_alternative<std::string>(*result));
  REQUIRE(std::get<std::string>(*result) == "token-123");
}

TEST_CASE("extractProgressToken extracts numeric progressToken", "[util][progress]")
{
  mcp::jsonrpc::Request request;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"]["progressToken"] = 42;

  const auto result = mcp::util::progress::extractProgressToken(request);
  REQUIRE(result.has_value());
  REQUIRE(std::holds_alternative<std::int64_t>(*result));
  REQUIRE(std::get<std::int64_t>(*result) == 42);
}

TEST_CASE("jsonToNumber handles double values", "[util][progress]")
{
  const mcp::jsonrpc::JsonValue value = 3.14159;

  const auto result = mcp::util::progress::jsonToNumber(value);
  REQUIRE(result.has_value());
  REQUIRE(*result == 3.14159);
}

TEST_CASE("jsonToNumber handles int64 values", "[util][progress]")
{
  const mcp::jsonrpc::JsonValue value = std::int64_t {-42};

  const auto result = mcp::util::progress::jsonToNumber(value);
  REQUIRE(result.has_value());
  REQUIRE(*result == -42.0);
}

TEST_CASE("jsonToNumber handles uint64 values", "[util][progress]")
{
  const mcp::jsonrpc::JsonValue value = std::uint64_t {123};

  const auto result = mcp::util::progress::jsonToNumber(value);
  REQUIRE(result.has_value());
  REQUIRE(*result == 123.0);
}

TEST_CASE("jsonToNumber returns nullopt for invalid types", "[util][progress]")
{
  // String
  const auto stringResult = mcp::util::progress::jsonToNumber(mcp::jsonrpc::JsonValue("not a number"));
  REQUIRE_FALSE(stringResult.has_value());

  // Object
  const auto objectResult = mcp::util::progress::jsonToNumber(mcp::jsonrpc::JsonValue::object());
  REQUIRE_FALSE(objectResult.has_value());

  // Array
  const auto arrayResult = mcp::util::progress::jsonToNumber(mcp::jsonrpc::JsonValue::array());
  REQUIRE_FALSE(arrayResult.has_value());

  // Boolean
  const auto boolResult = mcp::util::progress::jsonToNumber(mcp::jsonrpc::JsonValue(true));
  REQUIRE_FALSE(boolResult.has_value());

  // Null
  const auto nullResult = mcp::util::progress::jsonToNumber(mcp::jsonrpc::JsonValue(nullptr));
  REQUIRE_FALSE(nullResult.has_value());
}

TEST_CASE("jsonToRequestId handles string values", "[util][progress]")
{
  const mcp::jsonrpc::JsonValue value = "request-id";

  const auto result = mcp::util::progress::jsonToRequestId(value);
  REQUIRE(result.has_value());
  REQUIRE(std::holds_alternative<std::string>(*result));
  REQUIRE(std::get<std::string>(*result) == "request-id");
}

TEST_CASE("jsonToRequestId handles int64 values", "[util][progress]")
{
  const mcp::jsonrpc::JsonValue value = std::int64_t {-100};

  const auto result = mcp::util::progress::jsonToRequestId(value);
  REQUIRE(result.has_value());
  REQUIRE(std::holds_alternative<std::int64_t>(*result));
  REQUIRE(std::get<std::int64_t>(*result) == -100);
}

TEST_CASE("jsonToRequestId handles uint64 values within int64 range", "[util][progress]")
{
  const mcp::jsonrpc::JsonValue value = std::uint64_t {100};

  const auto result = mcp::util::progress::jsonToRequestId(value);
  REQUIRE(result.has_value());
  REQUIRE(std::holds_alternative<std::int64_t>(*result));
  REQUIRE(std::get<std::int64_t>(*result) == 100);
}

TEST_CASE("jsonToRequestId rejects uint64 values exceeding int64 max", "[util][progress]")
{
  const std::uint64_t tooLarge = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
  const mcp::jsonrpc::JsonValue value = tooLarge;

  const auto result = mcp::util::progress::jsonToRequestId(value);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("jsonToRequestId returns nullopt for invalid types", "[util][progress]")
{
  // Object
  const auto objectResult = mcp::util::progress::jsonToRequestId(mcp::jsonrpc::JsonValue::object());
  REQUIRE_FALSE(objectResult.has_value());

  // Array
  const auto arrayResult = mcp::util::progress::jsonToRequestId(mcp::jsonrpc::JsonValue::array());
  REQUIRE_FALSE(arrayResult.has_value());

  // Boolean
  const auto boolResult = mcp::util::progress::jsonToRequestId(mcp::jsonrpc::JsonValue(false));
  REQUIRE_FALSE(boolResult.has_value());

  // Null
  const auto nullResult = mcp::util::progress::jsonToRequestId(mcp::jsonrpc::JsonValue(nullptr));
  REQUIRE_FALSE(nullResult.has_value());

  // Double (not supported for request IDs)
  const auto doubleResult = mcp::util::progress::jsonToRequestId(mcp::jsonrpc::JsonValue(3.14));
  REQUIRE_FALSE(doubleResult.has_value());
}

TEST_CASE("requestIdToJson converts int64 to JSON", "[util][progress]")
{
  const mcp::jsonrpc::RequestId id = std::int64_t {12345};

  const auto result = mcp::util::progress::requestIdToJson(id);
  REQUIRE(result.is_int64());
  REQUIRE(result.as<std::int64_t>() == 12345);
}

TEST_CASE("requestIdToJson converts string to JSON", "[util][progress]")
{
  const mcp::jsonrpc::RequestId id = std::string("my-request-id");

  const auto result = mcp::util::progress::requestIdToJson(id);
  REQUIRE(result.is_string());
  REQUIRE(result.as<std::string>() == "my-request-id");
}

TEST_CASE("requestIdToJson and jsonToRequestId round-trip for int64", "[util][progress]")
{
  const mcp::jsonrpc::RequestId original = std::int64_t {9876543210LL};

  const auto json = mcp::util::progress::requestIdToJson(original);
  const auto recovered = mcp::util::progress::jsonToRequestId(json);

  REQUIRE(recovered.has_value());
  REQUIRE(std::holds_alternative<std::int64_t>(*recovered));
  REQUIRE(std::get<std::int64_t>(*recovered) == 9876543210LL);
}

TEST_CASE("requestIdToJson and jsonToRequestId round-trip for string", "[util][progress]")
{
  const mcp::jsonrpc::RequestId original = std::string("test-token-123");

  const auto json = mcp::util::progress::requestIdToJson(original);
  const auto recovered = mcp::util::progress::jsonToRequestId(json);

  REQUIRE(recovered.has_value());
  REQUIRE(std::holds_alternative<std::string>(*recovered));
  REQUIRE(std::get<std::string>(*recovered) == "test-token-123");
}

TEST_CASE("parseProgressNotification rejects wrong method", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = 0.5;

  const auto notification = test_detail::makeNotification("wrong/method", params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseProgressNotification rejects missing params", "[util][progress]")
{
  mcp::jsonrpc::Notification notification;
  notification.method = std::string(mcp::util::progress::kProgressNotificationMethod);
  // No params

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseProgressNotification rejects non-object params", "[util][progress]")
{
  mcp::jsonrpc::Notification notification;
  notification.method = std::string(mcp::util::progress::kProgressNotificationMethod);
  notification.params = mcp::jsonrpc::JsonValue::array();

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseProgressNotification rejects missing progressToken", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progress"] = 0.5;
  // Missing progressToken

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseProgressNotification rejects missing progress", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  // Missing progress

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseProgressNotification rejects invalid progressToken type", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = mcp::jsonrpc::JsonValue::object();  // Object not allowed
  params["progress"] = 0.5;

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseProgressNotification rejects invalid progress type", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = "not a number";

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseProgressNotification accepts minimal valid notification with string token", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "my-token";
  params["progress"] = 0.75;

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(std::holds_alternative<std::string>(result->progressToken));
  REQUIRE(std::get<std::string>(result->progressToken) == "my-token");
  REQUIRE(result->progress == 0.75);
  REQUIRE_FALSE(result->total.has_value());
  REQUIRE_FALSE(result->message.has_value());
}

TEST_CASE("parseProgressNotification accepts minimal valid notification with numeric token", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = 123;
  params["progress"] = 0.25;

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(std::holds_alternative<std::int64_t>(result->progressToken));
  REQUIRE(std::get<std::int64_t>(result->progressToken) == 123);
  REQUIRE(result->progress == 0.25);
}

TEST_CASE("parseProgressNotification handles int64 progress", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = std::int64_t {100};

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(result->progress == 100.0);
}

TEST_CASE("parseProgressNotification handles uint64 progress", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = std::uint64_t {200};

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(result->progress == 200.0);
}

TEST_CASE("parseProgressNotification handles optional total field", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = 0.5;
  params["total"] = 100.0;

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(result->total.has_value());
  REQUIRE(*result->total == 100.0);
}

TEST_CASE("parseProgressNotification handles int64 total", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = 50;
  params["total"] = std::int64_t {100};

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(result->total.has_value());
  REQUIRE(*result->total == 100.0);
}

TEST_CASE("parseProgressNotification handles optional message field", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = 0.5;
  params["message"] = "Processing step 1";

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(result->message.has_value());
  REQUIRE(*result->message == "Processing step 1");
}

TEST_CASE("parseProgressNotification ignores non-string message", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = 0.5;
  params["message"] = 123;  // Not a string

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->message.has_value());
}

TEST_CASE("parseProgressNotification captures additionalProperties excluding reserved keys", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = 0.5;
  params["total"] = 100.0;
  params["message"] = "test";
  params["customField1"] = "value1";
  params["customField2"] = 42;
  params["nestedObject"] = mcp::jsonrpc::JsonValue::object({{"key", "value"}});

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());

  // Reserved keys should NOT be in additionalProperties
  REQUIRE_FALSE(result->additionalProperties.contains("progressToken"));
  REQUIRE_FALSE(result->additionalProperties.contains("progress"));
  REQUIRE_FALSE(result->additionalProperties.contains("total"));
  REQUIRE_FALSE(result->additionalProperties.contains("message"));

  // Custom fields should be present
  REQUIRE(result->additionalProperties.contains("customField1"));
  REQUIRE(result->additionalProperties.at("customField1").as<std::string>() == "value1");
  REQUIRE(result->additionalProperties.contains("customField2"));
  REQUIRE(result->additionalProperties.at("customField2").as<int>() == 42);
  REQUIRE(result->additionalProperties.contains("nestedObject"));
  REQUIRE(result->additionalProperties.at("nestedObject").at("key").as<std::string>() == "value");
}

TEST_CASE("parseProgressNotification handles empty additionalProperties", "[util][progress]")
{
  mcp::jsonrpc::JsonValue params = mcp::jsonrpc::JsonValue::object();
  params["progressToken"] = "token";
  params["progress"] = 0.5;

  const auto notification = test_detail::makeNotification(std::string(mcp::util::progress::kProgressNotificationMethod), params);

  const auto result = mcp::util::progress::parseProgressNotification(notification);
  REQUIRE(result.has_value());
  REQUIRE(result->additionalProperties.empty());
}

TEST_CASE("makeProgressNotification creates notification with all fields", "[util][progress]")
{
  const mcp::jsonrpc::RequestId token = std::string("token-abc");
  const double progress = 0.75;
  const double total = 100.0;
  const std::string message = "Almost done";

  const auto notification = mcp::util::progress::makeProgressNotification(token, progress, total, message);

  REQUIRE(notification.method == mcp::util::progress::kProgressNotificationMethod);
  REQUIRE(notification.params.has_value());

  const auto &params = *notification.params;
  REQUIRE(params.contains("progressToken"));
  REQUIRE(params.at("progressToken").as<std::string>() == "token-abc");
  REQUIRE(params.contains("progress"));
  REQUIRE(params.at("progress").as<double>() == 0.75);
  REQUIRE(params.contains("total"));
  REQUIRE(params.at("total").as<double>() == 100.0);
  REQUIRE(params.contains("message"));
  REQUIRE(params.at("message").as<std::string>() == "Almost done");
}

TEST_CASE("makeProgressNotification omits null total and message", "[util][progress]")
{
  const mcp::jsonrpc::RequestId token = std::int64_t {42};
  const double progress = 0.5;

  const auto notification = mcp::util::progress::makeProgressNotification(token, progress, std::nullopt, std::nullopt);

  REQUIRE(notification.params.has_value());
  const auto &params = *notification.params;

  // Required fields should be present
  REQUIRE(params.contains("progressToken"));
  REQUIRE(params.contains("progress"));

  // Optional fields should NOT be present when nullopt
  REQUIRE_FALSE(params.contains("total"));
  REQUIRE_FALSE(params.contains("message"));
}

TEST_CASE("makeProgressNotification omits only total when nullopt", "[util][progress]")
{
  const mcp::jsonrpc::RequestId token = std::string("my-token");
  const double progress = 0.25;
  const std::string message = "Working...";

  const auto notification = mcp::util::progress::makeProgressNotification(token, progress, std::nullopt, message);

  REQUIRE(notification.params.has_value());
  const auto &params = *notification.params;

  REQUIRE(params.contains("progressToken"));
  REQUIRE(params.contains("progress"));
  REQUIRE_FALSE(params.contains("total"));  // Should be absent
  REQUIRE(params.contains("message"));  // Should be present
  REQUIRE(params.at("message").as<std::string>() == "Working...");
}

TEST_CASE("makeProgressNotification omits only message when nullopt", "[util][progress]")
{
  const mcp::jsonrpc::RequestId token = std::int64_t {123};
  const double progress = 0.5;
  const double total = 200.0;

  const auto notification = mcp::util::progress::makeProgressNotification(token, progress, total, std::nullopt);

  REQUIRE(notification.params.has_value());
  const auto &params = *notification.params;

  REQUIRE(params.contains("progressToken"));
  REQUIRE(params.contains("progress"));
  REQUIRE(params.contains("total"));  // Should be present
  REQUIRE_FALSE(params.contains("message"));  // Should be absent
}

TEST_CASE("makeProgressNotification uses correct method name", "[util][progress]")
{
  const mcp::jsonrpc::RequestId token = std::string("test");

  const auto notification = mcp::util::progress::makeProgressNotification(token, 0.0);

  REQUIRE(notification.method == "notifications/progress");
}

TEST_CASE("ProgressNotification struct has correct default values", "[util][progress]")
{
  mcp::util::progress::ProgressNotification notification;

  // Default progress should be 0.0
  REQUIRE(notification.progress == 0.0);

  // Optional fields should be nullopt
  REQUIRE_FALSE(notification.total.has_value());
  REQUIRE_FALSE(notification.message.has_value());

  // additionalProperties should be an empty object
  REQUIRE(notification.additionalProperties.is_object());
  REQUIRE(notification.additionalProperties.empty());
}

TEST_CASE("Round-trip: make then parse preserves all data", "[util][progress]")
{
  const mcp::jsonrpc::RequestId token = std::string("round-trip-token");
  const double progress = 0.85;
  const double total = 100.0;
  const std::string message = "Test message";

  // Make notification
  const auto notification = mcp::util::progress::makeProgressNotification(token, progress, total, message);

  // Parse it back
  const auto parsed = mcp::util::progress::parseProgressNotification(notification);

  REQUIRE(parsed.has_value());
  REQUIRE(std::holds_alternative<std::string>(parsed->progressToken));
  REQUIRE(std::get<std::string>(parsed->progressToken) == "round-trip-token");
  REQUIRE(parsed->progress == 0.85);
  REQUIRE(parsed->total.has_value());
  REQUIRE(*parsed->total == 100.0);
  REQUIRE(parsed->message.has_value());
  REQUIRE(*parsed->message == "Test message");
}

// NOLINTEND(readability-function-cognitive-complexity)
