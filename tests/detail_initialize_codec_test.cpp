#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/detail/initialize_codec.hpp>
#include <mcp/lifecycle/session.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity, misc-const-correctness, bugprone-unchecked-optional-access, google-build-using-namespace, abseil-string-find-str-contains,
// misc-include-cleaner)

namespace detail = mcp::detail;
using namespace mcp::lifecycle::session;

TEST_CASE("Icon to JSON encodes required src field", "[initialize_codec][icon]")
{
  Icon icon {"https://example.com/icon.png"};
  auto json = detail::iconToJson(icon);

  REQUIRE(json.is_object());
  REQUIRE(json.contains("src"));
  REQUIRE(json["src"].as<std::string>() == "https://example.com/icon.png");
}

TEST_CASE("Icon to JSON omits optional fields when not present", "[initialize_codec][icon]")
{
  Icon icon {"https://example.com/icon.png"};
  auto json = detail::iconToJson(icon);

  REQUIRE_FALSE(json.contains("mimeType"));
  REQUIRE_FALSE(json.contains("sizes"));
  REQUIRE_FALSE(json.contains("theme"));
}

TEST_CASE("Icon to JSON includes optional fields when present", "[initialize_codec][icon]")
{
  Icon icon {"https://example.com/icon.png", "image/png", std::vector<std::string> {"16x16", "32x32"}, "dark"};
  auto json = detail::iconToJson(icon);

  REQUIRE(json.contains("mimeType"));
  REQUIRE(json["mimeType"].as<std::string>() == "image/png");

  REQUIRE(json.contains("sizes"));
  REQUIRE(json["sizes"].is_array());
  REQUIRE(json["sizes"].size() == 2);

  REQUIRE(json.contains("theme"));
  REQUIRE(json["theme"].as<std::string>() == "dark");
}

TEST_CASE("Implementation to JSON encodes required name and version", "[initialize_codec][implementation]")
{
  Implementation impl {"test-client", "1.0.0"};
  auto json = detail::implementationToJson(impl);

  REQUIRE(json.is_object());
  REQUIRE(json.contains("name"));
  REQUIRE(json["name"].as<std::string>() == "test-client");
  REQUIRE(json.contains("version"));
  REQUIRE(json["version"].as<std::string>() == "1.0.0");
}

TEST_CASE("Implementation to JSON omits optional fields when not present", "[initialize_codec][implementation]")
{
  Implementation impl {"test-client", "1.0.0"};
  auto json = detail::implementationToJson(impl);

  REQUIRE_FALSE(json.contains("title"));
  REQUIRE_FALSE(json.contains("description"));
  REQUIRE_FALSE(json.contains("websiteUrl"));
  REQUIRE_FALSE(json.contains("icons"));
}

TEST_CASE("Implementation to JSON includes optional fields when present", "[initialize_codec][implementation]")
{
  Implementation impl {"test-client", "1.0.0", "Test Client", "A test client", "https://example.com", std::vector<Icon> {Icon {"https://example.com/icon.png"}}};
  auto json = detail::implementationToJson(impl);

  REQUIRE(json.contains("title"));
  REQUIRE(json["title"].as<std::string>() == "Test Client");

  REQUIRE(json.contains("description"));
  REQUIRE(json["description"].as<std::string>() == "A test client");

  REQUIRE(json.contains("websiteUrl"));
  REQUIRE(json["websiteUrl"].as<std::string>() == "https://example.com");

  REQUIRE(json.contains("icons"));
  REQUIRE(json["icons"].is_array());
  REQUIRE(json["icons"].size() == 1);
}

TEST_CASE("parseImplementation uses defaults for missing fields", "[initialize_codec][implementation]")
{
  jsoncons::json json = jsoncons::json::object();
  auto impl = detail::parseImplementation(json, "default-name", "default-version");

  REQUIRE(impl.name() == "default-name");
  REQUIRE(impl.version() == "default-version");
  REQUIRE_FALSE(impl.title().has_value());
}

TEST_CASE("parseImplementation parses all fields correctly", "[initialize_codec][implementation]")
{
  jsoncons::json json;
  json["name"] = "test-client";
  json["version"] = "1.0.0";
  json["title"] = "Test Client";
  json["description"] = "A test client";
  json["websiteUrl"] = "https://example.com";
  json["icons"] = jsoncons::json::array();
  json["icons"].push_back(jsoncons::json::object());
  json["icons"][0]["src"] = "https://example.com/icon.png";

  auto impl = detail::parseImplementation(json, "default-name", "default-version");

  REQUIRE(impl.name() == "test-client");
  REQUIRE(impl.version() == "1.0.0");
  REQUIRE(impl.title().has_value());
  REQUIRE(impl.title().value() == "Test Client");
  REQUIRE(impl.description().has_value());
  REQUIRE(impl.description().value() == "A test client");
  REQUIRE(impl.websiteUrl().has_value());
  REQUIRE(impl.websiteUrl().value() == "https://example.com");
  REQUIRE(impl.icons().has_value());
  REQUIRE(impl.icons().value().size() == 1);
}

TEST_CASE("parseImplementation returns defaults for non-object input", "[initialize_codec][implementation]")
{
  jsoncons::json json = "not-an-object";
  auto impl = detail::parseImplementation(json, "default-name", "default-version");

  REQUIRE(impl.name() == "default-name");
  REQUIRE(impl.version() == "default-version");
}

TEST_CASE("ClientCapabilities to JSON encodes expected shapes", "[initialize_codec][capabilities]")
{
  ClientCapabilities caps;
  caps = {RootsCapability {true}, std::nullopt, std::nullopt, std::nullopt, std::nullopt};

  auto json = detail::clientCapabilitiesToJson(caps);

  REQUIRE(json.is_object());
  REQUIRE(json.contains("roots"));
  REQUIRE(json["roots"].is_object());
  REQUIRE(json["roots"].contains("listChanged"));
  REQUIRE(json["roots"]["listChanged"].as<bool>() == true);
}

TEST_CASE("ClientCapabilities to JSON includes sampling with context and tools", "[initialize_codec][capabilities]")
{
  ClientCapabilities caps;
  caps = {std::nullopt, SamplingCapability {true, true}, std::nullopt, std::nullopt, std::nullopt};

  auto json = detail::clientCapabilitiesToJson(caps);

  REQUIRE(json.contains("sampling"));
  REQUIRE(json["sampling"].is_object());
  REQUIRE(json["sampling"].contains("context"));
  REQUIRE(json["sampling"].contains("tools"));
}

TEST_CASE("ClientCapabilities to JSON includes elicitation form mode", "[initialize_codec][capabilities]")
{
  ClientCapabilities caps;
  caps = {std::nullopt, std::nullopt, ElicitationCapability {true, false}, std::nullopt, std::nullopt};

  auto json = detail::clientCapabilitiesToJson(caps);

  REQUIRE(json.contains("elicitation"));
  REQUIRE(json["elicitation"].is_object());
  REQUIRE(json["elicitation"].contains("form"));
  REQUIRE_FALSE(json["elicitation"].contains("url"));
}

TEST_CASE("ClientCapabilities to JSON includes tasks with requests", "[initialize_codec][capabilities]")
{
  ClientCapabilities caps;
  caps = {std::nullopt, std::nullopt, std::nullopt, TasksCapability {true, true, true, false, true}, std::nullopt};

  auto json = detail::clientCapabilitiesToJson(caps);

  REQUIRE(json.contains("tasks"));
  REQUIRE(json["tasks"].is_object());
  REQUIRE(json["tasks"].contains("list"));
  REQUIRE(json["tasks"].contains("cancel"));
  REQUIRE(json["tasks"].contains("requests"));
  REQUIRE(json["tasks"]["requests"].contains("sampling"));
  REQUIRE(json["tasks"]["requests"].contains("tools"));
  REQUIRE(json["tasks"]["requests"]["tools"].contains("call"));
}

TEST_CASE("ClientCapabilities to JSON includes tasks with all request types", "[initialize_codec][capabilities]")
{
  ClientCapabilities caps;
  caps = {std::nullopt, std::nullopt, std::nullopt, TasksCapability {true, true, true, true, true}, std::nullopt};

  auto json = detail::clientCapabilitiesToJson(caps);

  REQUIRE(json["tasks"]["requests"].contains("sampling"));
  REQUIRE(json["tasks"]["requests"].contains("elicitation"));
  REQUIRE(json["tasks"]["requests"].contains("tools"));
}

TEST_CASE("parseClientCapabilities returns empty for non-object", "[initialize_codec][capabilities]")
{
  jsoncons::json json = "not-an-object";
  auto caps = detail::parseClientCapabilities(json);

  REQUIRE_FALSE(caps.roots().has_value());
  REQUIRE_FALSE(caps.sampling().has_value());
  REQUIRE_FALSE(caps.elicitation().has_value());
  REQUIRE_FALSE(caps.tasks().has_value());
}

TEST_CASE("parseClientCapabilities parses roots with listChanged", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["roots"] = jsoncons::json::object();
  json["roots"]["listChanged"] = true;

  auto caps = detail::parseClientCapabilities(json);

  REQUIRE(caps.roots().has_value());
  REQUIRE(caps.roots()->listChanged == true);
}

TEST_CASE("parseClientCapabilities parses sampling with context and tools", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["sampling"] = jsoncons::json::object();
  json["sampling"]["context"] = jsoncons::json::object();
  json["sampling"]["tools"] = jsoncons::json::object();

  auto caps = detail::parseClientCapabilities(json);

  REQUIRE(caps.sampling().has_value());
  REQUIRE(caps.sampling()->context == true);
  REQUIRE(caps.sampling()->tools == true);
}

TEST_CASE("parseClientCapabilities parses elicitation with empty object as form mode", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["elicitation"] = jsoncons::json::object();  // Empty object means form mode per SRS

  auto caps = detail::parseClientCapabilities(json);

  REQUIRE(caps.elicitation().has_value());
  REQUIRE(caps.elicitation()->form == true);
}

TEST_CASE("parseClientCapabilities parses tasks with requests.tools.call", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["tasks"] = jsoncons::json::object();
  json["tasks"]["list"] = jsoncons::json::object();
  json["tasks"]["cancel"] = jsoncons::json::object();
  json["tasks"]["requests"] = jsoncons::json::object();
  json["tasks"]["requests"]["tools"] = jsoncons::json::object();
  json["tasks"]["requests"]["tools"]["call"] = jsoncons::json::object();

  auto caps = detail::parseClientCapabilities(json);

  REQUIRE(caps.tasks().has_value());
  REQUIRE(caps.tasks()->list == true);
  REQUIRE(caps.tasks()->cancel == true);
  REQUIRE(caps.tasks()->toolsCall == true);
}

TEST_CASE("parseClientCapabilities preserves experimental objects", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["experimental"] = jsoncons::json::object();
  json["experimental"]["featureX"] = "value";

  auto caps = detail::parseClientCapabilities(json);

  REQUIRE(caps.experimental().has_value());
  REQUIRE(caps.experimental()->contains("featureX"));
}

TEST_CASE("ServerCapabilities to JSON encodes expected shapes", "[initialize_codec][capabilities]")
{
  ServerCapabilities caps;
  caps = {LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};

  auto json = detail::serverCapabilitiesToJson(caps);

  REQUIRE(json.is_object());
  REQUIRE(json.contains("logging"));
  REQUIRE(json["logging"].is_object());
}

TEST_CASE("ServerCapabilities to JSON includes prompts with listChanged", "[initialize_codec][capabilities]")
{
  ServerCapabilities caps;
  caps = {std::nullopt, std::nullopt, PromptsCapability {true}, std::nullopt, std::nullopt, std::nullopt, std::nullopt};

  auto json = detail::serverCapabilitiesToJson(caps);

  REQUIRE(json.contains("prompts"));
  REQUIRE(json["prompts"].is_object());
  REQUIRE(json["prompts"].contains("listChanged"));
  REQUIRE(json["prompts"]["listChanged"].as<bool>() == true);
}

TEST_CASE("ServerCapabilities to JSON includes resources with subscribe and listChanged", "[initialize_codec][capabilities]")
{
  ServerCapabilities caps;
  caps = {std::nullopt, std::nullopt, std::nullopt, ResourcesCapability {true, true}, std::nullopt, std::nullopt, std::nullopt};

  auto json = detail::serverCapabilitiesToJson(caps);

  REQUIRE(json.contains("resources"));
  REQUIRE(json["resources"].is_object());
  REQUIRE(json["resources"].contains("subscribe"));
  REQUIRE(json["resources"].contains("listChanged"));
}

TEST_CASE("ServerCapabilities to JSON includes tools with listChanged", "[initialize_codec][capabilities]")
{
  ServerCapabilities caps;
  caps = {std::nullopt, std::nullopt, std::nullopt, std::nullopt, ToolsCapability {true}, std::nullopt, std::nullopt};

  auto json = detail::serverCapabilitiesToJson(caps);

  REQUIRE(json.contains("tools"));
  REQUIRE(json["tools"].is_object());
  REQUIRE(json["tools"].contains("listChanged"));
}

TEST_CASE("ServerCapabilities to JSON includes tasks with requests.tools.call", "[initialize_codec][capabilities]")
{
  ServerCapabilities caps;
  caps = {std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, TasksCapability {true, true, false, false, true}, std::nullopt};

  auto json = detail::serverCapabilitiesToJson(caps);

  REQUIRE(json.contains("tasks"));
  REQUIRE(json["tasks"].is_object());
  REQUIRE(json["tasks"].contains("list"));
  REQUIRE(json["tasks"].contains("cancel"));
  REQUIRE(json["tasks"].contains("requests"));
  REQUIRE(json["tasks"]["requests"].contains("tools"));
  REQUIRE(json["tasks"]["requests"]["tools"].contains("call"));
}

TEST_CASE("parseServerCapabilities returns empty for non-object", "[initialize_codec][capabilities]")
{
  jsoncons::json json = "not-an-object";
  auto caps = detail::parseServerCapabilities(json);

  REQUIRE_FALSE(caps.logging().has_value());
  REQUIRE_FALSE(caps.prompts().has_value());
  REQUIRE_FALSE(caps.resources().has_value());
  REQUIRE_FALSE(caps.tools().has_value());
  REQUIRE_FALSE(caps.tasks().has_value());
}

TEST_CASE("parseServerCapabilities parses logging", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["logging"] = jsoncons::json::object();

  auto caps = detail::parseServerCapabilities(json);

  REQUIRE(caps.logging().has_value());
}

TEST_CASE("parseServerCapabilities parses prompts with listChanged", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["prompts"] = jsoncons::json::object();
  json["prompts"]["listChanged"] = true;

  auto caps = detail::parseServerCapabilities(json);

  REQUIRE(caps.prompts().has_value());
  REQUIRE(caps.prompts()->listChanged == true);
}

TEST_CASE("parseServerCapabilities parses resources with subscribe and listChanged", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["resources"] = jsoncons::json::object();
  json["resources"]["subscribe"] = true;
  json["resources"]["listChanged"] = true;

  auto caps = detail::parseServerCapabilities(json);

  REQUIRE(caps.resources().has_value());
  REQUIRE(caps.resources()->subscribe == true);
  REQUIRE(caps.resources()->listChanged == true);
}

TEST_CASE("parseServerCapabilities parses tools with listChanged", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["tools"] = jsoncons::json::object();
  json["tools"]["listChanged"] = true;

  auto caps = detail::parseServerCapabilities(json);

  REQUIRE(caps.tools().has_value());
  REQUIRE(caps.tools()->listChanged == true);
}

TEST_CASE("parseServerCapabilities parses tasks with requests.tools.call", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["tasks"] = jsoncons::json::object();
  json["tasks"]["list"] = jsoncons::json::object();
  json["tasks"]["cancel"] = jsoncons::json::object();
  json["tasks"]["requests"] = jsoncons::json::object();
  json["tasks"]["requests"]["tools"] = jsoncons::json::object();
  json["tasks"]["requests"]["tools"]["call"] = jsoncons::json::object();

  auto caps = detail::parseServerCapabilities(json);

  REQUIRE(caps.tasks().has_value());
  REQUIRE(caps.tasks()->list == true);
  REQUIRE(caps.tasks()->cancel == true);
  REQUIRE(caps.tasks()->toolsCall == true);
}

TEST_CASE("parseServerCapabilities preserves experimental objects", "[initialize_codec][capabilities]")
{
  jsoncons::json json;
  json["experimental"] = jsoncons::json::object();
  json["experimental"]["featureY"] = "value";

  auto caps = detail::parseServerCapabilities(json);

  REQUIRE(caps.experimental().has_value());
  REQUIRE(caps.experimental()->contains("featureY"));
}

TEST_CASE("Roundtrip: Icon encode then decode preserves data", "[initialize_codec][roundtrip]")
{
  Icon original {"https://example.com/icon.png", "image/png", std::vector<std::string> {"16x16", "32x32"}, "dark"};
  auto json = detail::iconToJson(original);

  // Since we don't have a parseIcon function exposed, just verify the JSON contains expected values
  REQUIRE(json["src"].as<std::string>() == "https://example.com/icon.png");
  REQUIRE(json["mimeType"].as<std::string>() == "image/png");
  REQUIRE(json["sizes"].size() == 2);
  REQUIRE(json["theme"].as<std::string>() == "dark");
}

TEST_CASE("Roundtrip: Implementation encode then parse preserves data", "[initialize_codec][roundtrip]")
{
  Implementation original {"test-client", "1.0.0", "Test Client", "A test client", "https://example.com", std::vector<Icon> {Icon {"https://example.com/icon.png", "image/png"}}};
  auto json = detail::implementationToJson(original);
  auto parsed = detail::parseImplementation(json, "default", "0.0.0");

  REQUIRE(parsed.name() == "test-client");
  REQUIRE(parsed.version() == "1.0.0");
  REQUIRE(parsed.title().has_value());
  REQUIRE(parsed.title().value() == "Test Client");
  REQUIRE(parsed.description().has_value());
  REQUIRE(parsed.description().value() == "A test client");
  REQUIRE(parsed.websiteUrl().has_value());
  REQUIRE(parsed.websiteUrl().value() == "https://example.com");
  REQUIRE(parsed.icons().has_value());
  REQUIRE(parsed.icons().value().size() == 1);
}

TEST_CASE("Roundtrip: ClientCapabilities encode then parse preserves data", "[initialize_codec][roundtrip]")
{
  ClientCapabilities original;
  original = {RootsCapability {true}, SamplingCapability {true, true}, ElicitationCapability {true, false}, TasksCapability {true, true, true, false, true}, std::nullopt};

  auto json = detail::clientCapabilitiesToJson(original);
  auto parsed = detail::parseClientCapabilities(json);

  REQUIRE(parsed.roots().has_value());
  REQUIRE(parsed.roots()->listChanged == true);
  REQUIRE(parsed.sampling().has_value());
  REQUIRE(parsed.sampling()->context == true);
  REQUIRE(parsed.sampling()->tools == true);
  REQUIRE(parsed.elicitation().has_value());
  REQUIRE(parsed.elicitation()->form == true);
  REQUIRE(parsed.elicitation()->url == false);
  REQUIRE(parsed.tasks().has_value());
  REQUIRE(parsed.tasks()->list == true);
  REQUIRE(parsed.tasks()->cancel == true);
  REQUIRE(parsed.tasks()->samplingCreateMessage == true);
  REQUIRE(parsed.tasks()->toolsCall == true);
}

TEST_CASE("Roundtrip: ServerCapabilities encode then parse preserves data", "[initialize_codec][roundtrip]")
{
  ServerCapabilities original;
  original = {LoggingCapability {},
              CompletionsCapability {},
              PromptsCapability {true},
              ResourcesCapability {true, true},
              ToolsCapability {true},
              TasksCapability {true, true, false, false, true},
              std::nullopt};

  auto json = detail::serverCapabilitiesToJson(original);
  auto parsed = detail::parseServerCapabilities(json);

  REQUIRE(parsed.logging().has_value());
  REQUIRE(parsed.completions().has_value());
  REQUIRE(parsed.prompts().has_value());
  REQUIRE(parsed.prompts()->listChanged == true);
  REQUIRE(parsed.resources().has_value());
  REQUIRE(parsed.resources()->subscribe == true);
  REQUIRE(parsed.resources()->listChanged == true);
  REQUIRE(parsed.tools().has_value());
  REQUIRE(parsed.tools()->listChanged == true);
  REQUIRE(parsed.tasks().has_value());
  REQUIRE(parsed.tasks()->list == true);
  REQUIRE(parsed.tasks()->cancel == true);
  REQUIRE(parsed.tasks()->toolsCall == true);
}

// NOLINTEND(readability-function-cognitive-complexity, misc-const-correctness, bugprone-unchecked-optional-access, google-build-using-namespace, abseil-string-find-str-contains,
// misc-include-cleaner)
