#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/version.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity, misc-const-correctness, bugprone-unchecked-optional-access, google-build-using-namespace, abseil-string-find-str-contains,
// misc-include-cleaner)

using namespace mcp;

TEST_CASE("Session starts in Created state", "[lifecycle][session]")
{
  Session session;
  REQUIRE(session.state() == SessionState::kCreated);
}

TEST_CASE("Session supports configured protocol versions", "[lifecycle][session]")
{
  SessionOptions options;
  options.supportedProtocolVersions = {"2025-11-25", "2024-11-05"};

  Session session(options);

  const auto &versions = session.supportedProtocolVersions();
  REQUIRE(versions.size() == 2);
  REQUIRE(versions[0] == "2025-11-25");
  REQUIRE(versions[1] == "2024-11-05");
}

TEST_CASE("Session defaults to standard protocol versions", "[lifecycle][session]")
{
  Session session;

  const auto &versions = session.supportedProtocolVersions();
  REQUIRE(versions.size() >= 2);
  REQUIRE(std::find(versions.begin(), versions.end(), std::string(kLatestProtocolVersion)) != versions.end());
  REQUIRE(std::find(versions.begin(), versions.end(), std::string(kLegacyProtocolVersion)) != versions.end());
}

TEST_CASE("Client must send initialize as first request", "[lifecycle][client][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kClient);
  REQUIRE(session.state() == SessionState::kCreated);
  REQUIRE(session.canSendRequest("initialize"));
  REQUIRE_FALSE(session.canSendRequest("ping"));

  // Trying to send any request other than initialize should throw
  jsoncons::json params = jsoncons::json::object();
  REQUIRE_THROWS_AS(session.sendRequest("ping", params), LifecycleError);
  REQUIRE_THROWS_AS(session.sendRequest("tools/list", params), LifecycleError);
  REQUIRE(session.state() == SessionState::kCreated);

  // Sending initialize should succeed (doesn't actually send, just validates)
  // Note: sendRequest currently returns a placeholder future, so it won't throw for initialize
  REQUIRE_NOTHROW(session.sendRequest("initialize", params));
  REQUIRE(session.state() == SessionState::kInitializing);
}

TEST_CASE("Client can only send ping while waiting for initialize response", "[lifecycle][client][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kClient);

  // First send initialize to transition to Initializing state
  jsoncons::json initParams;
  initParams["protocolVersion"] = std::string(kLatestProtocolVersion);
  initParams["capabilities"] = jsoncons::json::object();
  initParams["clientInfo"] = jsoncons::json::object();
  initParams["clientInfo"]["name"] = "test-client";
  initParams["clientInfo"]["version"] = "1.0.0";

  REQUIRE_NOTHROW(session.sendRequest("initialize", initParams));
  REQUIRE(session.state() == SessionState::kInitializing);

  // Now only ping should be allowed
  REQUIRE(session.canSendRequest("ping"));
  REQUIRE_FALSE(session.canSendRequest("initialize"));
  REQUIRE_FALSE(session.canSendRequest("tools/list"));

  jsoncons::json params = jsoncons::json::object();
  REQUIRE_NOTHROW(session.sendRequest("ping", params));
  REQUIRE_THROWS_AS(session.sendRequest("initialize", params), LifecycleError);
  REQUIRE_THROWS_AS(session.sendRequest("tools/list", params), LifecycleError);
}

TEST_CASE("Client cannot send initialized notification before initialization completes", "[lifecycle][client][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kClient);

  // Should not be able to send initialized notification before initialize completes.
  jsoncons::json initParams = jsoncons::json::object();
  REQUIRE_NOTHROW(session.sendRequest("initialize", initParams));
  REQUIRE(session.state() == SessionState::kInitializing);

  REQUIRE_THROWS_AS(session.sendNotification("notifications/initialized"), LifecycleError);
}

TEST_CASE("Server enforces initialize as first request", "[lifecycle][server][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kServer);
  REQUIRE(session.state() == SessionState::kCreated);

  // Server should not accept non-initialize requests before initialization
  // Ping IS allowed before initialization according to the spec
  REQUIRE_FALSE(session.canHandleRequest("tools/list"));
  REQUIRE(session.canHandleRequest("ping"));
  REQUIRE(session.canHandleRequest("initialize"));

  // Try to handle a non-initialize request via initialize handler
  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "ping";
  request.params = jsoncons::json::object();

  // Server should reject because method is not initialize
  auto response = session.handleInitializeRequest(request);
  REQUIRE(std::holds_alternative<jsonrpc::ErrorResponse>(response));

  const auto &errorResp = std::get<jsonrpc::ErrorResponse>(response);
  REQUIRE(errorResp.error.code == -32600);
}

TEST_CASE("Server handles initialize request with supported version", "[lifecycle][server][negotiation]")
{
  Session session;
  session.setRole(SessionRole::kServer);

  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "initialize";
  request.params = jsoncons::json::object();
  (*request.params)["protocolVersion"] = std::string(kLatestProtocolVersion);
  (*request.params)["capabilities"] = jsoncons::json::object();
  (*request.params)["clientInfo"] = jsoncons::json::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";

  auto response = session.handleInitializeRequest(request);
  REQUIRE(std::holds_alternative<jsonrpc::SuccessResponse>(response));

  const auto &successResp = std::get<jsonrpc::SuccessResponse>(response);
  REQUIRE(successResp.result.contains("protocolVersion"));
  REQUIRE(successResp.result["protocolVersion"].as<std::string>() == std::string(kLatestProtocolVersion));
  REQUIRE(successResp.result.contains("capabilities"));
  REQUIRE(successResp.result.contains("serverInfo"));

  // Server should transition to Initialized state
  REQUIRE(session.state() == SessionState::kInitialized);
  REQUIRE(session.negotiatedProtocolVersion().has_value());
  REQUIRE(*session.negotiatedProtocolVersion() == kLatestProtocolVersion);
}

TEST_CASE("Server negotiates version when client proposes unsupported version", "[lifecycle][server][negotiation]")
{
  Session session;
  session.setRole(SessionRole::kServer);

  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "initialize";
  request.params = jsoncons::json::object();
  (*request.params)["protocolVersion"] = "2023-01-01";  // Unsupported version
  (*request.params)["capabilities"] = jsoncons::json::object();
  (*request.params)["clientInfo"] = jsoncons::json::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";

  auto response = session.handleInitializeRequest(request);
  REQUIRE(std::holds_alternative<jsonrpc::SuccessResponse>(response));

  const auto &successResp = std::get<jsonrpc::SuccessResponse>(response);
  REQUIRE(successResp.result.contains("protocolVersion"));
  REQUIRE(successResp.result["protocolVersion"].as<std::string>() == std::string(kLatestProtocolVersion));
}

TEST_CASE("Server returns actionable error when it supports no protocol versions", "[lifecycle][server][negotiation]")
{
  SessionOptions options;
  options.supportedProtocolVersions.clear();

  Session session(options);
  session.setRole(SessionRole::kServer);

  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "initialize";
  request.params = jsoncons::json::object();
  (*request.params)["protocolVersion"] = "2025-11-25";
  (*request.params)["capabilities"] = jsoncons::json::object();
  (*request.params)["clientInfo"] = jsoncons::json::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";

  auto response = session.handleInitializeRequest(request);
  REQUIRE(std::holds_alternative<jsonrpc::ErrorResponse>(response));

  const auto &errorResp = std::get<jsonrpc::ErrorResponse>(response);
  REQUIRE(errorResp.error.code == -32602);
  REQUIRE(errorResp.error.message.find("Protocol negotiation failed") != std::string::npos);
  REQUIRE(errorResp.error.data.has_value());
  const auto &errorData = *errorResp.error.data;
  REQUIRE(errorData.contains("supported"));
  REQUIRE(errorData.contains("requested"));
  REQUIRE(errorData["requested"].as<std::string>() == "2025-11-25");
  REQUIRE(errorData["supported"].is_array());
  REQUIRE(errorData["supported"].empty());
}

TEST_CASE("Server rejects requests before initialization", "[lifecycle][server][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kServer);

  // Before initialization, only initialize and ping should be allowed
  REQUIRE(session.canHandleRequest("initialize"));
  REQUIRE(session.canHandleRequest("ping"));
  REQUIRE_FALSE(session.canHandleRequest("tools/list"));
  REQUIRE_FALSE(session.canHandleRequest("resources/read"));
}

TEST_CASE("Server pre-init restrictions allow only lifecycle-safe traffic", "[lifecycle][server][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kServer);

  SECTION("created state allows only initialize and ping requests")
  {
    REQUIRE(session.state() == SessionState::kCreated);
    REQUIRE(session.canHandleRequest("initialize"));
    REQUIRE(session.canHandleRequest("ping"));
    REQUIRE_FALSE(session.canHandleRequest("logging/setLevel"));
    REQUIRE_FALSE(session.canHandleRequest("tools/list"));

    REQUIRE(session.canSendRequest("ping"));
    REQUIRE_FALSE(session.canSendRequest("tools/list"));

    jsoncons::json params = jsoncons::json::object();
    REQUIRE_NOTHROW(session.sendRequest("ping", params));
    REQUIRE_THROWS_AS(session.sendRequest("tools/list", params), LifecycleError);

    REQUIRE_FALSE(session.canSendNotification("notifications/message"));
    REQUIRE_THROWS_AS(session.sendNotification("notifications/message"), LifecycleError);
  }

  SECTION("initialized state allows ping and logging before operating")
  {
    jsonrpc::Request request;
    request.id = std::int64_t {1};
    request.method = "initialize";
    request.params = jsoncons::json::object();
    (*request.params)["protocolVersion"] = std::string(kLatestProtocolVersion);
    (*request.params)["capabilities"] = jsoncons::json::object();
    (*request.params)["clientInfo"] = jsoncons::json::object();
    (*request.params)["clientInfo"]["name"] = "test-client";
    (*request.params)["clientInfo"]["version"] = "1.0.0";

    const auto initResponse = session.handleInitializeRequest(request);
    REQUIRE(std::holds_alternative<jsonrpc::SuccessResponse>(initResponse));
    REQUIRE(session.state() == SessionState::kInitialized);

    REQUIRE_FALSE(session.canHandleRequest("initialize"));
    REQUIRE(session.canHandleRequest("ping"));
    REQUIRE(session.canHandleRequest("logging/setLevel"));
    REQUIRE_FALSE(session.canHandleRequest("tools/list"));

    REQUIRE(session.canSendRequest("ping"));
    REQUIRE_FALSE(session.canSendRequest("tools/list"));

    REQUIRE(session.canSendNotification("notifications/message"));
    REQUIRE_NOTHROW(session.sendNotification("notifications/message"));
  }
}

TEST_CASE("Server transitions to operating after initialized notification", "[lifecycle][server]")
{
  Session session;
  session.setRole(SessionRole::kServer);

  // First, complete initialization
  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "initialize";
  request.params = jsoncons::json::object();
  (*request.params)["protocolVersion"] = std::string(kLatestProtocolVersion);
  (*request.params)["capabilities"] = jsoncons::json::object();
  (*request.params)["clientInfo"] = jsoncons::json::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";

  session.handleInitializeRequest(request);
  REQUIRE(session.state() == SessionState::kInitialized);

  // After initialized notification, server should transition to Operating
  session.handleInitializedNotification();
  REQUIRE(session.state() == SessionState::kOperating);

  // Now all requests should be allowed
  REQUIRE(session.canHandleRequest("tools/list"));
  REQUIRE(session.canHandleRequest("resources/read"));
  REQUIRE(session.canHandleRequest("ping"));
}

TEST_CASE("Client automatically transitions to operating after initialize response", "[lifecycle][client][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kClient);

  jsoncons::json initParams = jsoncons::json::object();
  REQUIRE_NOTHROW(session.sendRequest("initialize", initParams));
  REQUIRE(session.state() == SessionState::kInitializing);

  jsonrpc::SuccessResponse initResponse;
  initResponse.id = std::int64_t {1};
  initResponse.result = jsoncons::json::object();
  initResponse.result["protocolVersion"] = std::string(kLatestProtocolVersion);
  initResponse.result["capabilities"] = jsoncons::json::object();
  initResponse.result["serverInfo"] = jsoncons::json::object();
  initResponse.result["serverInfo"]["name"] = "test-server";
  initResponse.result["serverInfo"]["version"] = "1.0.0";

  REQUIRE_NOTHROW(session.handleInitializeResponse(jsonrpc::Response {initResponse}));
  REQUIRE(session.state() == SessionState::kOperating);

  jsoncons::json params = jsoncons::json::object();
  REQUIRE_NOTHROW(session.sendRequest("tools/list", params));

  REQUIRE_THROWS_AS(session.sendNotification("notifications/initialized"), LifecycleError);
}

TEST_CASE("Server selects latest supported version regardless of list order", "[lifecycle][server][negotiation]")
{
  SessionOptions options;
  options.supportedProtocolVersions = {"2024-11-05", "2025-11-25"};

  Session session(options);
  session.setRole(SessionRole::kServer);

  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "initialize";
  request.params = jsoncons::json::object();
  (*request.params)["protocolVersion"] = "2023-01-01";
  (*request.params)["capabilities"] = jsoncons::json::object();
  (*request.params)["clientInfo"] = jsoncons::json::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";

  auto response = session.handleInitializeRequest(request);
  REQUIRE(std::holds_alternative<jsonrpc::SuccessResponse>(response));

  const auto &successResp = std::get<jsonrpc::SuccessResponse>(response);
  REQUIRE(successResp.result["protocolVersion"].as<std::string>() == "2025-11-25");
}

TEST_CASE("Client negotiation failure reports actionable version error", "[lifecycle][client][negotiation]")
{
  Session session;
  session.setRole(SessionRole::kClient);

  jsoncons::json initParams = jsoncons::json::object();
  REQUIRE_NOTHROW(session.sendRequest("initialize", initParams));

  jsonrpc::SuccessResponse initResponse;
  initResponse.id = std::int64_t {1};
  initResponse.result = jsoncons::json::object();
  initResponse.result["protocolVersion"] = "1900-01-01";
  initResponse.result["capabilities"] = jsoncons::json::object();
  initResponse.result["serverInfo"] = jsoncons::json::object();
  initResponse.result["serverInfo"]["name"] = "test-server";
  initResponse.result["serverInfo"]["version"] = "1.0.0";

  try
  {
    session.handleInitializeResponse(jsonrpc::Response {initResponse});
    FAIL("Expected LifecycleError");
  }
  catch (const LifecycleError &error)
  {
    const std::string errorMessage = error.what();
    REQUIRE(errorMessage.find("unsupported protocol version") != std::string::npos);
    REQUIRE(errorMessage.find("1900-01-01") != std::string::npos);
    REQUIRE(errorMessage.find(std::string(kLatestProtocolVersion)) != std::string::npos);
    REQUIRE(errorMessage.find(std::string(kLegacyProtocolVersion)) != std::string::npos);
    REQUIRE(session.state() == SessionState::kCreated);
  }
}

TEST_CASE("Capability negotiation preserves experimental capabilities", "[lifecycle][capabilities][negotiation]")
{
  Session serverSession;
  serverSession.setRole(SessionRole::kServer);

  jsonrpc::Request initializeRequest;
  initializeRequest.id = std::int64_t {1};
  initializeRequest.method = "initialize";
  initializeRequest.params = jsoncons::json::object();
  (*initializeRequest.params)["protocolVersion"] = std::string(kLatestProtocolVersion);
  (*initializeRequest.params)["capabilities"] = jsoncons::json::object();
  (*initializeRequest.params)["capabilities"]["roots"] = jsoncons::json::object();
  (*initializeRequest.params)["capabilities"]["experimental"] = jsoncons::json::object();
  (*initializeRequest.params)["capabilities"]["experimental"]["x-test"] = jsoncons::json::object();
  (*initializeRequest.params)["capabilities"]["experimental"]["x-test"]["enabled"] = true;
  (*initializeRequest.params)["capabilities"]["experimental"]["x-test"]["cohort"] = "beta";
  (*initializeRequest.params)["clientInfo"] = jsoncons::json::object();
  (*initializeRequest.params)["clientInfo"]["name"] = "test-client";
  (*initializeRequest.params)["clientInfo"]["version"] = "1.0.0";

  auto initServerResponse = serverSession.handleInitializeRequest(initializeRequest);
  REQUIRE(std::holds_alternative<jsonrpc::SuccessResponse>(initServerResponse));
  REQUIRE(serverSession.checkCapability("roots"));
  REQUIRE(serverSession.checkCapability("experimental"));
  REQUIRE(serverSession.negotiatedParameters().has_value());
  REQUIRE(serverSession.negotiatedParameters()->clientCapabilities().experimental().has_value());
  const auto &clientExperimental = *serverSession.negotiatedParameters()->clientCapabilities().experimental();
  REQUIRE(clientExperimental["x-test"]["enabled"].as<bool>());
  REQUIRE(clientExperimental["x-test"]["cohort"].as<std::string>() == "beta");

  Session clientSession;
  clientSession.setRole(SessionRole::kClient);
  jsoncons::json initParams = jsoncons::json::object();
  REQUIRE_NOTHROW(clientSession.sendRequest("initialize", initParams));

  jsonrpc::SuccessResponse initializeResponse;
  initializeResponse.id = std::int64_t {2};
  initializeResponse.result = jsoncons::json::object();
  initializeResponse.result["protocolVersion"] = std::string(kLatestProtocolVersion);
  initializeResponse.result["capabilities"] = jsoncons::json::object();
  initializeResponse.result["capabilities"]["tools"] = jsoncons::json::object();
  initializeResponse.result["capabilities"]["experimental"] = jsoncons::json::object();
  initializeResponse.result["capabilities"]["experimental"]["y-test"] = jsoncons::json::object();
  initializeResponse.result["capabilities"]["experimental"]["y-test"]["enabled"] = true;
  initializeResponse.result["capabilities"]["experimental"]["y-test"]["maxBatch"] = 8;
  initializeResponse.result["serverInfo"] = jsoncons::json::object();
  initializeResponse.result["serverInfo"]["name"] = "test-server";
  initializeResponse.result["serverInfo"]["version"] = "1.0.0";

  REQUIRE_NOTHROW(clientSession.handleInitializeResponse(jsonrpc::Response {initializeResponse}));
  REQUIRE(clientSession.checkCapability("tools"));
  REQUIRE(clientSession.checkCapability("experimental"));
  REQUIRE(clientSession.negotiatedParameters().has_value());
  REQUIRE(clientSession.negotiatedParameters()->serverCapabilities().experimental().has_value());
  const auto &serverExperimental = *clientSession.negotiatedParameters()->serverCapabilities().experimental();
  REQUIRE(serverExperimental["y-test"]["enabled"].as<bool>());
  REQUIRE(serverExperimental["y-test"]["maxBatch"].as<int>() == 8);
}

TEST_CASE("Elicitation fallback parsing only enables form for explicit empty object", "[lifecycle][capabilities][elicitation]")
{
  auto negotiateClientElicitation = [](const jsoncons::json &elicitationCapabilities) -> ElicitationCapability
  {
    Session session;
    session.setRole(SessionRole::kServer);

    jsonrpc::Request initializeRequest;
    initializeRequest.id = std::int64_t {1};
    initializeRequest.method = "initialize";
    initializeRequest.params = jsoncons::json::object();
    (*initializeRequest.params)["protocolVersion"] = std::string(kLatestProtocolVersion);
    (*initializeRequest.params)["capabilities"] = jsoncons::json::object();
    (*initializeRequest.params)["capabilities"]["elicitation"] = elicitationCapabilities;
    (*initializeRequest.params)["clientInfo"] = jsoncons::json::object();
    (*initializeRequest.params)["clientInfo"]["name"] = "test-client";
    (*initializeRequest.params)["clientInfo"]["version"] = "1.0.0";

    const auto response = session.handleInitializeRequest(initializeRequest);
    REQUIRE(std::holds_alternative<jsonrpc::SuccessResponse>(response));
    REQUIRE(session.negotiatedParameters().has_value());

    const auto clientElicitation = session.negotiatedParameters()->clientCapabilities().elicitation();
    REQUIRE(clientElicitation.has_value());
    return *clientElicitation;
  };

  SECTION("empty elicitation object maps to legacy form support")
  {
    const ElicitationCapability capability = negotiateClientElicitation(jsoncons::json::object());
    REQUIRE(capability.form);
    REQUIRE_FALSE(capability.url);
  }

  SECTION("unknown elicitation object shape does not enable form fallback")
  {
    jsoncons::json elicitation = jsoncons::json::object();
    elicitation["legacy"] = jsoncons::json::object();

    const ElicitationCapability capability = negotiateClientElicitation(elicitation);
    REQUIRE_FALSE(capability.form);
    REQUIRE_FALSE(capability.url);
  }

  SECTION("malformed form declaration does not enable form fallback")
  {
    jsoncons::json elicitation = jsoncons::json::object();
    elicitation["form"] = true;

    const ElicitationCapability capability = negotiateClientElicitation(elicitation);
    REQUIRE_FALSE(capability.form);
    REQUIRE_FALSE(capability.url);
  }
}

TEST_CASE("Server allows logging notifications in initialized state", "[lifecycle][server][enforcement]")
{
  Session session;
  session.setRole(SessionRole::kServer);

  // Complete initialization
  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "initialize";
  request.params = jsoncons::json::object();
  (*request.params)["protocolVersion"] = std::string(kLatestProtocolVersion);
  (*request.params)["capabilities"] = jsoncons::json::object();
  (*request.params)["clientInfo"] = jsoncons::json::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";

  session.handleInitializeRequest(request);

  // Server should allow logging notifications in initialized state
  REQUIRE(session.canSendNotification("notifications/message"));
}

TEST_CASE("Client capability checking", "[lifecycle][capabilities]")
{
  Session session;
  session.setRole(SessionRole::kServer);

  // Before negotiation, no capabilities should be available
  REQUIRE_FALSE(session.checkCapability("roots"));
  REQUIRE_FALSE(session.checkCapability("sampling"));

  // Complete initialization (without specific capabilities)
  jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "initialize";
  request.params = jsoncons::json::object();
  (*request.params)["protocolVersion"] = std::string(kLatestProtocolVersion);
  (*request.params)["capabilities"] = jsoncons::json::object();
  (*request.params)["clientInfo"] = jsoncons::json::object();
  (*request.params)["clientInfo"]["name"] = "test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";

  session.handleInitializeRequest(request);

  // After negotiation, still no capabilities declared
  REQUIRE_FALSE(session.checkCapability("roots"));
  REQUIRE_FALSE(session.checkCapability("sampling"));
}

TEST_CASE("Server capability checking", "[lifecycle][capabilities]")
{
  Session session;
  session.setRole(SessionRole::kClient);

  // Before negotiation, no capabilities should be available
  REQUIRE_FALSE(session.checkCapability("tools"));
  REQUIRE_FALSE(session.checkCapability("resources"));
}

TEST_CASE("Session stop transitions to stopped state", "[lifecycle][session]")
{
  Session session;

  session.stop();
  REQUIRE(session.state() == SessionState::kStopped);

  // Stopping again should be safe
  REQUIRE_NOTHROW(session.stop());
  REQUIRE(session.state() == SessionState::kStopped);
}

TEST_CASE("Implementation metadata storage", "[lifecycle][metadata]")
{
  Implementation impl("test-server", "1.0.0");
  REQUIRE(impl.name() == "test-server");
  REQUIRE(impl.version() == "1.0.0");
  REQUIRE_FALSE(impl.title().has_value());

  Implementation implWithTitle("test-server", "1.0.0", "Test Server Title");
  REQUIRE(implWithTitle.title().has_value());
  REQUIRE(*implWithTitle.title() == "Test Server Title");
}

TEST_CASE("ClientCapabilities construction and access", "[lifecycle][capabilities]")
{
  RootsCapability roots {.listChanged = true};
  SamplingCapability sampling {.context = true, .tools = true};
  ElicitationCapability elicitation {.form = true, .url = true};

  ClientCapabilities caps;
  caps = ClientCapabilities(roots, sampling, elicitation, std::nullopt, std::nullopt);

  REQUIRE(caps.roots().has_value());
  REQUIRE(caps.roots()->listChanged);
  REQUIRE(caps.sampling().has_value());
  REQUIRE(caps.sampling()->context);
  REQUIRE(caps.sampling()->tools);
  REQUIRE(caps.elicitation().has_value());
  REQUIRE(caps.elicitation()->form);
  REQUIRE(caps.elicitation()->url);
}

TEST_CASE("ServerCapabilities construction and access", "[lifecycle][capabilities]")
{
  PromptsCapability prompts {.listChanged = true};
  ResourcesCapability resources {.subscribe = true, .listChanged = true};
  ToolsCapability tools {.listChanged = true};

  ServerCapabilities caps;
  caps = ServerCapabilities(std::nullopt, std::nullopt, prompts, resources, tools, std::nullopt, std::nullopt);

  REQUIRE(caps.prompts().has_value());
  REQUIRE(caps.prompts()->listChanged);
  REQUIRE(caps.resources().has_value());
  REQUIRE(caps.resources()->subscribe);
  REQUIRE(caps.resources()->listChanged);
  REQUIRE(caps.tools().has_value());
  REQUIRE(caps.tools()->listChanged);
}

TEST_CASE("Capability hasCapability check", "[lifecycle][capabilities]")
{
  RootsCapability roots;
  ClientCapabilities clientCaps(roots, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  REQUIRE(clientCaps.hasCapability("roots"));
  REQUIRE_FALSE(clientCaps.hasCapability("sampling"));
  REQUIRE_FALSE(clientCaps.hasCapability("elicitation"));

  ToolsCapability tools;
  ServerCapabilities serverCaps(std::nullopt, std::nullopt, std::nullopt, std::nullopt, tools, std::nullopt, std::nullopt);

  REQUIRE(serverCaps.hasCapability("tools"));
  REQUIRE_FALSE(serverCaps.hasCapability("resources"));
  REQUIRE_FALSE(serverCaps.hasCapability("prompts"));
}

TEST_CASE("NegotiatedParameters stores negotiated values", "[lifecycle][negotiation]")
{
  ClientCapabilities clientCaps;
  ServerCapabilities serverCaps;
  Implementation clientInfo("test-client", "1.0.0");
  Implementation serverInfo("test-server", "1.0.0");

  NegotiatedParameters params("2025-11-25", clientCaps, serverCaps, clientInfo, serverInfo, "Test instructions");

  REQUIRE(params.protocolVersion() == "2025-11-25");
  REQUIRE(params.clientInfo().name() == "test-client");
  REQUIRE(params.serverInfo().name() == "test-server");
  REQUIRE(params.instructions().has_value());
  REQUIRE(*params.instructions() == "Test instructions");
}

// NOLINTEND(readability-function-cognitive-complexity, misc-const-correctness, bugprone-unchecked-optional-access, google-build-using-namespace, abseil-string-find-str-contains,
// misc-include-cleaner)
