#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/version.hpp>

namespace
{

namespace mcp_transport = mcp::transport;
namespace mcp_http = mcp::transport::http;

auto makeHeaderedRequest(mcp_http::ServerRequestMethod method,
                         std::string path,
                         std::optional<std::string_view> body = std::nullopt,
                         std::optional<std::string_view> sessionId = std::nullopt,
                         std::optional<std::string_view> lastEventId = std::nullopt,
                         std::optional<std::string_view> origin = std::nullopt,
                         std::optional<std::string_view> protocolVersion = std::nullopt) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = method;
  request.path = std::move(path);

  if (body.has_value())
  {
    request.body = std::string(*body);
    mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
  }

  if (sessionId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderMcpSessionId, std::string(*sessionId));
  }

  if (lastEventId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderLastEventId, std::string(*lastEventId));
  }

  if (origin.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderOrigin, std::string(*origin));
  }

  if (protocolVersion.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderMcpProtocolVersion, std::string(*protocolVersion));
  }

  return request;
}

auto makeRequestBody(std::int64_t id, std::string method) -> std::string
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = std::move(method);
  request.params = mcp::jsonrpc::JsonValue::object();
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {request});
}

auto makeInitializeMessage(std::int64_t id) -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = "initialize";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  (*request.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"]["name"] = "conformance-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";
  return mcp::jsonrpc::Message {request};
}

auto makeNotificationBody(std::string method) -> std::string
{
  mcp::jsonrpc::Notification notification;
  notification.method = std::move(method);
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {notification});
}

auto makeSuccessResponseBody(std::int64_t id) -> std::string
{
  mcp::jsonrpc::SuccessResponse response;
  response.id = id;
  response.result = mcp::jsonrpc::JsonValue::object();
  response.result["ok"] = true;
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {response});
}

auto makeInitializeResponseBody(std::int64_t id) -> mcp::jsonrpc::SuccessResponse
{
  mcp::jsonrpc::SuccessResponse response;
  response.id = id;
  response.result = mcp::jsonrpc::JsonValue::object();
  response.result["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  response.result["capabilities"] = mcp::jsonrpc::JsonValue::object();
  response.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
  response.result["serverInfo"]["name"] = "conformance-server";
  response.result["serverInfo"]["version"] = "1.0.0";
  return response;
}

auto makeInitializeHttpResponse(std::int64_t id, std::optional<std::string_view> sessionId = std::nullopt) -> mcp_http::ServerResponse
{
  const mcp::jsonrpc::SuccessResponse responseBody = makeInitializeResponseBody(id);

  mcp_http::ServerResponse response;
  response.statusCode = 200;
  response.body = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {responseBody});
  mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
  if (sessionId.has_value())
  {
    mcp_http::setHeader(response.headers, mcp_http::kHeaderMcpSessionId, std::string(*sessionId));
  }
  return response;
}

auto messageFromSseEvent(const mcp::http::sse::Event &event) -> mcp::jsonrpc::Message
{
  return mcp::jsonrpc::parseMessage(event.data);
}

auto parseSseResponseBody(const mcp_http::ServerResponse &response) -> std::vector<mcp::http::sse::Event>
{
  REQUIRE(response.sse.has_value());
  const std::vector<mcp::http::sse::Event> parsed = mcp::http::sse::parseEvents(response.body);
  REQUIRE(parsed.size() == response.sse->events.size());
  return parsed;
}

class GeneratedTlsFixture
{
public:
  GeneratedTlsFixture()
  {
    const auto uniqueSuffix = std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    rootPath_ = std::filesystem::current_path() / "tmp" / ("conformance-tls-" + uniqueSuffix);
    std::filesystem::create_directories(rootPath_);

    certificatePath_ = rootPath_ / "tls-localhost-cert.pem";
    privateKeyPath_ = rootPath_ / "tls-localhost-key.pem";
    caCertificatePath_ = rootPath_ / "tls-ca-cert.pem";

    writePem(certificatePath_, kLocalhostCertificatePem);
    writePem(privateKeyPath_, kLocalhostPrivateKeyPem);
    writePem(caCertificatePath_, kCaCertificatePem);
  }

  ~GeneratedTlsFixture()
  {
    std::error_code error;
    std::filesystem::remove_all(rootPath_, error);
  }

  GeneratedTlsFixture(const GeneratedTlsFixture &) = delete;
  auto operator=(const GeneratedTlsFixture &) -> GeneratedTlsFixture & = delete;

  [[nodiscard]] auto certificatePath() const -> std::string { return certificatePath_.string(); }
  [[nodiscard]] auto privateKeyPath() const -> std::string { return privateKeyPath_.string(); }
  [[nodiscard]] auto caCertificatePath() const -> std::string { return caCertificatePath_.string(); }

private:
  static constexpr std::string_view kLocalhostCertificatePem = R"(-----BEGIN CERTIFICATE-----
MIIDnjCCAoagAwIBAgIUbGJHCOH88mkDB2gfRamd/APzqnswDQYJKoZIhvcNAQEL
BQAwXDELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx
FTATBgNVBAoMDE1DUCBTREsgVGVzdDEYMBYGA1UEAwwPTUNQIFNESyBUZXN0IENB
MB4XDTI2MDIxMTAyNDIzOFoXDTM2MDIwOTAyNDIzOFowVjELMAkGA1UEBhMCVVMx
DTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3QxFTATBgNVBAoMDE1DUCBTREsg
VGVzdDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A
MIIBCgKCAQEAqHDf/rvWPvrctXf5t95e3n2rDExpx+g80Tj69y+zyJO9Ub6Zc6ug
XVWHEygFbkEluGlIaUn4IX2Diursa8jIwI+xtNruSbj1unCXTGio3ajnJ0Ie/03o
yEBRwGNh1iYf81FgeLVEEq2S6tUW7Omxh3oEkSzn25Pv/Sf8OIcbZA1S/7Y6KTD0
AL6uOFHW2geTHZJYx/5SwP5tlP09EBceMqvhmQsUh4Qwy7V+IiOy/fnuqnV1d5tx
G7Z/JRwXEhTVTZv3RVzK+58TdwxKVK0zhjw+arT02PksLs1ryxFIb6n+bkoeN5mK
NadRNfRm+wA2/bZ4QEYs8//OYNnuifgSBwIDAQABo14wXDAaBgNVHREEEzARggls
b2NhbGhvc3SHBH8AAAEwHQYDVR0OBBYEFO0oQZHtoyrsACqGChlzLjBSbCFKMB8G
A1UdIwQYMBaAFJfSbJoJAk+WOsp2AnRqSlY+CAhoMA0GCSqGSIb3DQEBCwUAA4IB
AQBTe08wKjlppSRnrij3Qak0xqoeqK72OoaNFHeY0pFleCgK8yxzMVG3fcxcE3iP
wWoXcQ6s3AbW1uVjgEEDCXv4eJrnEndhd0RHU5g5DycDc2SVEFtOUqN+XZj8VbZh
hUAxAzWKdlWTmRGTcEtetdNDiLuVHfWYedEOdsh9flnvs9Kb+8eLarru5+FmKg+2
cLYzuCbEUH+FJD78L9sEO4QvKbSuhT5HqCNU9OBlrxQuNOKohMrDpxrlsJPmfOHI
jN9nyS0Roinc3gmZ8iSNubz7Ekv6gybmDJtHg/nMMQUz/irbF5xrsQ/azQg5f/6g
IJxHSEw2BbIZgzMm6gLsytK9
-----END CERTIFICATE-----
)";

  static constexpr std::string_view kLocalhostPrivateKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCocN/+u9Y++ty1
d/m33l7efasMTGnH6DzROPr3L7PIk71Rvplzq6BdVYcTKAVuQSW4aUhpSfghfYOK
6uxryMjAj7G02u5JuPW6cJdMaKjdqOcnQh7/TejIQFHAY2HWJh/zUWB4tUQSrZLq
1Rbs6bGHegSRLOfbk+/9J/w4hxtkDVL/tjopMPQAvq44UdbaB5MdkljH/lLA/m2U
/T0QFx4yq+GZCxSHhDDLtX4iI7L9+e6qdXV3m3Ebtn8lHBcSFNVNm/dFXMr7nxN3
DEpUrTOGPD5qtPTY+SwuzWvLEUhvqf5uSh43mYo1p1E19Gb7ADb9tnhARizz/85g
2e6J+BIHAgMBAAECggEAE4iDcVb3EXamAaVYICp5dfO80kBKRBR6eQjSkbfGcYww
eDpSMUfF4Qz6DQ6nEpIbfbc33n3leHzFtGZp5FX0ceA4C3EYERSR6n0EDqhUbOAZ
bkQMtC6kwBQ1ZsofnmXtbpoRRe4/ZqsaToFHl1HCdKOBxBQhDyWovwLUDPoeAwwY
OEBXRgRSiTsDiwRXd8VIYgbaEHgJsB6eufMxgrrlmsNJ/+/z8mymd5CbJr5h7uCg
FTAT7xic+F0iaP3bmRbwmCh/NC2bbHPOAFtTOYKO++sabushvhwNmnrswYGJSORd
MKAz1vGg1XeTsCvvQj5DDDS31Xz1d8wnZv0q1ad6gQKBgQDfsQuRYRYJodBfxeYh
pGwKfI03I6NaN+adsN3WSUxrQzaq8SdhJDcYt5mboPle8PvjJIG0CO4f8MIQ1j/a
Ucg7Zvk1bWvcSQMeEPVnDpJ1Vvy8xnQzIahrtLakV2mA/4/ZhmOzI7a93po94gii
O3PrxCi5QF8dHpmRV1mab+yD8wKBgQDAxPJa9YZquQSDD4ccR4CUx0+jSB/iKfVb
uo8d0bEWczNNLtR6t+VREKMRrzGU0RVBOOCVzi/f9Lg+rKDMaoVnRS8sqsbobjBb
HqJawd4YPXW55L07NPbkXcbFao0B1PboYCxUvXhdE/Az8U5qRDZsSXXldcVHL1lU
pRpn8p/CnQKBgF6jWeCM8bTrh+wtHvsWxDr/jQNKCZ9uzRvkK0awxisPSb6yvlVj
7AeCDfQA++AGFpt344QWzyAmTQSwkF0+gndXTpIjFCIjpbT/ucN7L82DGvSHBbxo
PggrcaY/8TwJY4PFTsMIlhToa9tImRyCCL4zxILz0AnS22fZS+iB98+vAoGAdD5P
Z0pnpDOt5NqYPxVfFFicTXpQv7FNo+L8Kp8oisEtTn3O47HBNwExVJiw7WynxIzn
4W5UsFiCQkkXLi2OBJhTujvBdqf7wPbYMKJ3q2Zkd5TYB7wIpe1mz+VQ4qnpundE
RFV9H6PGVYxOHQbFSseBsL00GZkT5VcqdUR50oECgYBl+V0HE2KUJGHBLQngjwFl
/Qfw1ElCF8xF9CSdOJ3cKwDjrZfoSPaliIVNVMEzRZW7gb0gyrFlB3ZgcdcqwB/z
kKhwcKyOcfrRLKast5QIFueKXfkQ85yI04KySnooXj/R4rOTkDuQB1QEhICPNOtU
lfKjpp76WRToPKKu6GmJsw==
-----END PRIVATE KEY-----
)";

  static constexpr std::string_view kCaCertificatePem = R"(-----BEGIN CERTIFICATE-----
MIIDmTCCAoGgAwIBAgIUJWFLkuelpQ0KWPU4B240zZdyfeAwDQYJKoZIhvcNAQEL
BQAwXDELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx
FTATBgNVBAoMDE1DUCBTREsgVGVzdDEYMBYGA1UEAwwPTUNQIFNESyBUZXN0IENB
MB4XDTI2MDIxMTAyNDIzOFoXDTM2MDIwOTAyNDIzOFowXDELMAkGA1UEBhMCVVMx
DTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3QxFTATBgNVBAoMDE1DUCBTREsg
VGVzdDEYMBYGA1UEAwwPTUNQIFNESyBUZXN0IENBMIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAiWdwq2hpeM8NFXxYH8+bMWvfA6Uib6hNzwa1jLhWbaxX
Z2Xtaa8URzjdBHcSFTQNMljGf8ULlLW7uGItbabR+g3sOEfH43Lg3a/S4VEV7CU5
2jJ2jlPZRQn5txmXUQXa0ZzHDUiY10NMI5GQ7LZWiJn/O+bCx3Y/XcWBoqGptc/H
TBdsAwlpPOnLzCVjt4mUUh+OTklDmjr/4Hdr0eb6FCaIsDcfKNceEZGNkd/waB1Q
bpDwIWj8IJFvP3pdtSZeMvbGxbZLMUymVUeyXWu2Bor8yLTFuBcfErCWkmnGsXcP
t395K9yfCT35RCVJWTe9ZUoAhAG64VIM7Zwnpg1rfQIDAQABo1MwUTAdBgNVHQ4E
FgQUl9JsmgkCT5Y6ynYCdGpKVj4ICGgwHwYDVR0jBBgwFoAUl9JsmgkCT5Y6ynYC
dGpKVj4ICGgwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEANLeQ
wsi0zIzvU8Sdk91U2llNCxVByHWXMe5ckCu6b+g1xvXNt+TevhDDvJumMcd/8FwD
aaE7ixU/f9EdD/pXiFahdBtJ/rJIjalk2diOQHK02s3+JnpsyyV4ZMQxt4L4Yk5x
7p8HebW9QgezTiUSeLkSz2gFqQs4S84mMwCrfa5lsX4zPRDaGFVvLSZ85y7rd/ZU
xTH94MOyT++8IV8IGtnrMJyxHTvjsmPZmmf4fZo8QIzTd7LwzgtXKb5qT93WJAWj
lzcmRyPdF7l9g6XeeVSRjnBdQzBK1GaBlMpi7O6N0+xdBVXI5jJiZzyfrfnPzr2j
9UdVOPSELrCu7OlGww==
-----END CERTIFICATE-----
)";

  static auto writePem(const std::filesystem::path &path, std::string_view pem) -> void
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
      throw std::runtime_error("Failed to create TLS fixture file: " + path.string());
    }

    output.write(pem.data(), static_cast<std::streamsize>(pem.size()));
    output.close();
    if (!output)
    {
      throw std::runtime_error("Failed to write TLS fixture file: " + path.string());
    }
  }

  std::filesystem::path rootPath_;
  std::filesystem::path certificatePath_;
  std::filesystem::path privateKeyPath_;
  std::filesystem::path caCertificatePath_;
};

auto makeRuntimePostRequest() -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = mcp_http::ServerRequestMethod::kPost;
  request.path = "/mcp";
  request.body = "{}";
  mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
  return request;
}

}  // namespace

TEST_CASE("Streamable HTTP enforces POST one-message semantics and 202 acceptance", "[conformance][transport][http]")
{
  mcp_http::StreamableHttpServer server;

  SECTION("POST body must contain exactly one JSON-RPC message")
  {
    const mcp_http::ServerResponse batchResponse =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])"));
    REQUIRE(batchResponse.statusCode == 400);

    const mcp_http::ServerResponse concatenatedResponse = server.handleRequest(
      makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", R"({"jsonrpc":"2.0","id":1,"method":"ping"}{"jsonrpc":"2.0","id":2,"method":"ping"})"));
    REQUIRE(concatenatedResponse.statusCode == 400);
  }

  SECTION("notifications and responses return HTTP 202 Accepted")
  {
    const mcp_http::ServerResponse notificationResponse =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized")));
    REQUIRE(notificationResponse.statusCode == 202);
    REQUIRE(notificationResponse.body.empty());

    const mcp_http::ServerResponse responseMessageResponse = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeSuccessResponseBody(33)));
    REQUIRE(responseMessageResponse.statusCode == 202);
    REQUIRE(responseMessageResponse.body.empty());
  }
}

TEST_CASE("Streamable HTTP GET SSE listen supports 405 and Last-Event-ID resume", "[conformance][transport][http]")
{
  SECTION("GET opens an SSE stream and emits priming event")
  {
    mcp_http::StreamableHttpServer server;
    const mcp_http::ServerResponse opened = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));

    REQUIRE(opened.statusCode == 200);
    REQUIRE(mcp_http::getHeader(opened.headers, mcp_http::kHeaderContentType) == std::optional<std::string> {"text/event-stream"});

    const std::vector<mcp::http::sse::Event> openedEvents = parseSseResponseBody(opened);
    REQUIRE(openedEvents.size() == 1);
    REQUIRE(openedEvents.front().id.has_value());
    REQUIRE(openedEvents.front().data.empty());

    mcp::jsonrpc::Notification outbound;
    outbound.method = "notifications/initialized";
    REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {outbound}));

    const mcp_http::ServerResponse resumed =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, std::nullopt, *openedEvents.front().id));

    REQUIRE(resumed.statusCode == 200);
    const std::vector<mcp::http::sse::Event> resumedEvents = parseSseResponseBody(resumed);
    REQUIRE(resumedEvents.size() == 1);

    const mcp::jsonrpc::Message resumedMessage = messageFromSseEvent(resumedEvents.front());
    REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(resumedMessage));
    REQUIRE(std::get<mcp::jsonrpc::Notification>(resumedMessage).method == "notifications/initialized");
  }

  SECTION("GET listen returns 405 when server disables GET SSE")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.allowGetSse = false;

    mcp_http::StreamableHttpServer server(options);
    const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));
    REQUIRE(response.statusCode == 405);
  }
}

TEST_CASE("Streamable HTTP enforces MCP session and protocol version rules", "[conformance][transport][http]")
{
  SECTION("missing required session returns HTTP 400")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.http.requireSessionId = true;

    mcp_http::StreamableHttpServer server(options);
    const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));
    REQUIRE(response.statusCode == 400);
  }

  SECTION("terminated session returns HTTP 404")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.http.requireSessionId = true;

    mcp_http::StreamableHttpServer server(options);
    server.upsertSession("terminated-session", mcp_http::SessionLookupState::kTerminated, std::string(mcp::kLatestProtocolVersion));

    const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "terminated-session"));
    REQUIRE(response.statusCode == 404);
  }

  SECTION("invalid session and protocol headers are rejected")
  {
    mcp_http::StreamableHttpServer server;

    const mcp_http::ServerResponse invalidSession =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), "bad session"));
    REQUIRE(invalidSession.statusCode == 400);

    const mcp_http::ServerResponse invalidProtocol = server.handleRequest(
      makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), std::nullopt, std::nullopt, std::nullopt, "2025/11/25"));
    REQUIRE(invalidProtocol.statusCode == 400);

    const mcp_http::ServerResponse unsupportedProtocol = server.handleRequest(
      makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), std::nullopt, std::nullopt, std::nullopt, "1900-01-01"));
    REQUIRE(unsupportedProtocol.statusCode == 400);
  }

  SECTION("missing protocol header falls back only when version cannot be inferred")
  {
    mcp_http::HeaderList noHeaders;
    mcp_http::RequestValidationResult fallbackResult = mcp_http::validateServerRequest(noHeaders, mcp_http::RequestValidationOptions {});
    REQUIRE(fallbackResult.accepted);
    REQUIRE(fallbackResult.effectiveProtocolVersion == std::string(mcp::kFallbackProtocolVersion));

    mcp_http::HeaderList sessionHeaders;
    mcp_http::setHeader(sessionHeaders, mcp_http::kHeaderMcpSessionId, "session-live");

    mcp_http::RequestValidationOptions options;
    options.sessionRequired = true;
    options.requestKind = mcp_http::RequestKind::kOther;
    options.sessionResolver = [](std::string_view sessionId) -> mcp_http::SessionResolution
    {
      if (sessionId == "session-live")
      {
        return {
          mcp_http::SessionLookupState::kActive,
          std::string(mcp::kLatestProtocolVersion),
        };
      }

      return {
        mcp_http::SessionLookupState::kUnknown,
        std::nullopt,
      };
    };

    mcp_http::RequestValidationResult inferredResult = mcp_http::validateServerRequest(sessionHeaders, options);
    REQUIRE(inferredResult.accepted);
    REQUIRE(inferredResult.effectiveProtocolVersion == std::string(mcp::kLatestProtocolVersion));
  }

  SECTION("client sends MCP-Protocol-Version after initialize and reinitializes without stale session")
  {
    std::vector<mcp_http::ServerRequest> observedRequests;

    auto executor = [&observedRequests](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
    {
      observedRequests.push_back(request);

      if (request.method != mcp_http::ServerRequestMethod::kPost)
      {
        mcp_http::ServerResponse response;
        response.statusCode = 405;
        return response;
      }

      const mcp::jsonrpc::Message message = mcp::jsonrpc::parseMessage(request.body);
      if (!std::holds_alternative<mcp::jsonrpc::Request>(message) || std::get<mcp::jsonrpc::Request>(message).method != "initialize")
      {
        mcp_http::ServerResponse accepted;
        accepted.statusCode = 202;
        return accepted;
      }

      const auto staleSession = mcp_http::getHeader(request.headers, mcp_http::kHeaderMcpSessionId);
      if (staleSession.has_value())
      {
        mcp_http::ServerResponse terminated;
        terminated.statusCode = 404;
        return terminated;
      }

      const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(message);
      return makeInitializeHttpResponse(std::get<std::int64_t>(initializeRequest.id), "new-session");
    };

    mcp_http::StreamableHttpClientOptions staleSessionOptions;
    staleSessionOptions.endpointUrl = "http://localhost/mcp";
    auto staleHeaderState = std::make_shared<mcp_http::SharedHeaderState>();
    REQUIRE(staleHeaderState->captureFromInitializeResponse("stale-session", std::string(mcp::kLatestProtocolVersion)));
    staleSessionOptions.headerState = staleHeaderState;

    mcp_http::StreamableHttpClient staleClient(std::move(staleSessionOptions), executor);
    REQUIRE_THROWS(staleClient.send(makeInitializeMessage(1)));

    REQUIRE(observedRequests.size() == 1);
    REQUIRE(mcp_http::getHeader(observedRequests[0].headers, mcp_http::kHeaderMcpSessionId) == std::optional<std::string> {"stale-session"});
    // Note: MCP-Protocol-Version is NOT sent for initialize requests (it's negotiated in the response)
    REQUIRE_FALSE(mcp_http::getHeader(observedRequests[0].headers, mcp_http::kHeaderMcpProtocolVersion).has_value());

    mcp_http::StreamableHttpClientOptions reinitOptions;
    reinitOptions.endpointUrl = "http://localhost/mcp";
    auto reinitHeaderState = std::make_shared<mcp_http::SharedHeaderState>();
    // Pre-set protocol version to simulate a client that already negotiated
    reinitHeaderState->captureFromInitializeResponse(std::nullopt, std::string(mcp::kLatestProtocolVersion));
    reinitOptions.headerState = reinitHeaderState;

    mcp_http::StreamableHttpClient reinitClient(std::move(reinitOptions), executor);
    // Initialize request also doesn't send protocol version header
    const mcp_http::StreamableHttpSendResult initializeResult = reinitClient.send(makeInitializeMessage(2));
    REQUIRE(initializeResult.statusCode == 200);
    REQUIRE(initializeResult.response.has_value());

    REQUIRE(observedRequests.size() == 2);
    // For initialize request: no session ID, no protocol version
    REQUIRE_FALSE(mcp_http::getHeader(observedRequests[1].headers, mcp_http::kHeaderMcpSessionId).has_value());
    REQUIRE_FALSE(mcp_http::getHeader(observedRequests[1].headers, mcp_http::kHeaderMcpProtocolVersion).has_value());

    mcp::jsonrpc::Notification initialized;
    initialized.method = "notifications/initialized";
    // Post-initialize notification should include the negotiated protocol version
    const mcp_http::StreamableHttpSendResult postInitNotification = reinitClient.send(mcp::jsonrpc::Message {initialized});
    REQUIRE(postInitNotification.statusCode == 202);
    REQUIRE(observedRequests.size() == 3);
    REQUIRE(mcp_http::getHeader(observedRequests[2].headers, mcp_http::kHeaderMcpProtocolVersion) == std::optional<std::string> {std::string(mcp::kLatestProtocolVersion)});
  }
}

TEST_CASE("Streamable HTTP origin validation rejects disallowed origins with HTTP 403", "[conformance][transport][http]")
{
  mcp_http::StreamableHttpServer server;
  const mcp_http::ServerResponse response = server.handleRequest(
    makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), std::nullopt, std::nullopt, "https://evil.example"));

  REQUIRE(response.statusCode == 403);
}

TEST_CASE("Streamable HTTP TLS handshake and verification", "[conformance][transport][http][tls]")
{
  GeneratedTlsFixture tlsFixture;

  mcp_transport::HttpServerOptions serverOptions;
  serverOptions.endpoint.path = "/mcp";
  serverOptions.endpoint.bindAddress = "127.0.0.1";
  serverOptions.endpoint.bindLocalhostOnly = true;
  serverOptions.endpoint.port = 0;

  mcp_http::ServerTlsConfiguration tls;
  tls.certificateChainFile = tlsFixture.certificatePath();
  tls.privateKeyFile = tlsFixture.privateKeyPath();
  serverOptions.tls = std::move(tls);

  mcp_transport::HttpServerRuntime server(std::move(serverOptions));
  server.setRequestHandler(
    [](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
    {
      mcp_http::ServerResponse response;
      response.statusCode = 200;
      response.body = "ok";
      mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "text/plain");
      return response;
    });
  server.start();

  mcp_transport::HttpClientOptions verifiedClientOptions;
  verifiedClientOptions.endpointUrl = "https://localhost:" + std::to_string(server.localPort()) + "/mcp";
  verifiedClientOptions.tls.caCertificateFile = tlsFixture.caCertificatePath();

  const mcp_transport::HttpClientRuntime verifiedClient(std::move(verifiedClientOptions));
  const mcp_http::ServerResponse verifiedResponse = verifiedClient.execute(makeRuntimePostRequest());
  REQUIRE(verifiedResponse.statusCode == 200);
  REQUIRE(verifiedResponse.body == "ok");

  mcp_transport::HttpClientOptions defaultClientOptions;
  defaultClientOptions.endpointUrl = "https://localhost:" + std::to_string(server.localPort()) + "/mcp";

  const mcp_transport::HttpClientRuntime defaultClient(std::move(defaultClientOptions));
  REQUIRE_THROWS(defaultClient.execute(makeRuntimePostRequest()));
}

TEST_CASE("Streamable HTTP session issuance and multi-session routing", "[conformance][transport][http]")
{
  mcp_http::StreamableHttpServerOptions serverOptions;
  serverOptions.http.requireSessionId = true;

  mcp_http::StreamableHttpServer server(serverOptions);
  server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;

      if (request.method == "initialize")
      {
        result.response = makeInitializeResponseBody(std::get<std::int64_t>(request.id));
      }
      else
      {
        // For other requests, just return an error (we're testing transport, not handler)
        mcp::jsonrpc::ErrorResponse error;
        error.id = request.id;
        error.error.code = -32601;  // Method not found
        error.error.message = "Method not found";
        result.response = error;
      }

      return result;
    });

  // Helper to make initialize request
  auto makeInitializeRequest = [&server](std::int64_t id) -> mcp_http::ServerResponse
  {
    mcp::jsonrpc::Request request;
    request.id = id;
    request.method = "initialize";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
    (*request.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
    (*request.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
    (*request.params)["clientInfo"]["name"] = "conformance-client";
    (*request.params)["clientInfo"]["version"] = "1.0.0";

    const std::string body = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {request});
    return server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body));
  };

  // Step 1: Issue initialize request A without session ID, expect MCP-Session-Id in response
  const mcp_http::ServerResponse initResponseA = makeInitializeRequest(1);
  REQUIRE(initResponseA.statusCode == 200);
  const std::optional<std::string> sessionIdA = mcp_http::getHeader(initResponseA.headers, mcp_http::kHeaderMcpSessionId);
  REQUIRE(sessionIdA.has_value());
  REQUIRE_FALSE(sessionIdA->empty());

  // Step 2: Issue initialize request B without session ID, expect different MCP-Session-Id
  const mcp_http::ServerResponse initResponseB = makeInitializeRequest(2);
  REQUIRE(initResponseB.statusCode == 200);
  const std::optional<std::string> sessionIdB = mcp_http::getHeader(initResponseB.headers, mcp_http::kHeaderMcpSessionId);
  REQUIRE(sessionIdB.has_value());
  REQUIRE_FALSE(sessionIdB->empty());

  // Verify the two session IDs are different
  REQUIRE(*sessionIdA != *sessionIdB);

  // Step 3: Send notifications/initialized with each session ID, both should return HTTP 202
  const mcp_http::ServerResponse notificationA =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), *sessionIdA));
  REQUIRE(notificationA.statusCode == 202);

  const mcp_http::ServerResponse notificationB =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), *sessionIdB));
  REQUIRE(notificationB.statusCode == 202);

  // Step 4: Send a non-initialize request without MCP-Session-Id, expect HTTP 400
  const std::string pingBody = makeRequestBody(3, "ping");
  const mcp_http::ServerResponse missingSessionResponse = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", pingBody));
  REQUIRE(missingSessionResponse.statusCode == 400);

  // Step 5: Send HTTP DELETE to terminate session A, expect HTTP 204
  const mcp_http::ServerResponse deleteResponse = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kDelete, "/mcp", std::nullopt, *sessionIdA));
  REQUIRE(deleteResponse.statusCode == 204);

  // Step 6: After termination, requests with session A should return HTTP 404 (both GET and POST)
  const mcp_http::ServerResponse terminatedGetResponse = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, *sessionIdA));
  REQUIRE(terminatedGetResponse.statusCode == 404);

  const mcp_http::ServerResponse terminatedNotification =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), *sessionIdA));
  REQUIRE(terminatedNotification.statusCode == 404);

  // But session B should still work
  const mcp_http::ServerResponse stillActiveNotification =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeNotificationBody("notifications/initialized"), *sessionIdB));
  REQUIRE(stillActiveNotification.statusCode == 202);
}
