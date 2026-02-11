#include <cstdint>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/transport/http.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace
{

namespace mcp_transport = mcp::transport;
namespace mcp_http = mcp::transport::http;

constexpr const char *kFixtureDir = MCP_TEST_TLS_FIXTURE_DIR;

auto fixturePath(const std::string &name) -> std::string
{
  return std::string(kFixtureDir) + "/" + name;
}

auto makePostRequest() -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = mcp_http::ServerRequestMethod::kPost;
  request.path = "/mcp";
  request.body = "{}";
  mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
  return request;
}

auto makeOkHandler() -> mcp_transport::HttpRequestHandler
{
  return [](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
  {
    mcp_http::ServerResponse response;
    response.statusCode = 200;
    response.body = "ok";
    mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "text/plain");
    return response;
  };
}

auto makeServerOptions() -> mcp_transport::HttpServerOptions
{
  mcp_transport::HttpServerOptions options;
  options.endpoint.path = "/mcp";
  options.endpoint.bindAddress = "127.0.0.1";
  options.endpoint.bindLocalhostOnly = true;
  options.endpoint.port = 0;
  return options;
}

auto makeClientOptions(std::string endpointUrl) -> mcp_transport::HttpClientOptions
{
  mcp_transport::HttpClientOptions options;
  options.endpointUrl = std::move(endpointUrl);
  return options;
}

auto endpointUrl(const std::string &scheme, std::uint16_t port) -> std::string
{
  return scheme + "://localhost:" + std::to_string(port) + "/mcp";
}

}  // namespace

TEST_CASE("HTTP runtime server runs in HTTP or HTTPS mode from config", "[transport][http][tls]")
{
  SECTION("HTTP mode")
  {
    mcp_transport::HttpServerRuntime server(makeServerOptions());
    server.setRequestHandler(makeOkHandler());
    server.start();

    const mcp_transport::HttpClientRuntime client(makeClientOptions(endpointUrl("http", server.localPort())));
    const mcp_http::ServerResponse response = client.execute(makePostRequest());

    REQUIRE(response.statusCode == 200);
    REQUIRE(response.body == "ok");
  }

  SECTION("HTTPS mode")
  {
    mcp_transport::HttpServerOptions serverOptions = makeServerOptions();
    mcp_http::ServerTlsConfiguration tls;
    tls.certificateChainFile = fixturePath("tls-localhost-cert.pem");
    tls.privateKeyFile = fixturePath("tls-localhost-key.pem");
    serverOptions.tls = std::move(tls);

    mcp_transport::HttpServerRuntime server(std::move(serverOptions));
    server.setRequestHandler(makeOkHandler());
    server.start();

    mcp_transport::HttpClientOptions clientOptions = makeClientOptions(endpointUrl("https", server.localPort()));
    clientOptions.tls.caCertificateFile = fixturePath("tls-ca-cert.pem");

    const mcp_transport::HttpClientRuntime client(std::move(clientOptions));
    const mcp_http::ServerResponse response = client.execute(makePostRequest());

    REQUIRE(response.statusCode == 200);
    REQUIRE(response.body == "ok");
  }
}

TEST_CASE("HTTPS client verification is enabled by default", "[transport][http][tls]")
{
  mcp_transport::HttpServerOptions serverOptions = makeServerOptions();
  mcp_http::ServerTlsConfiguration tls;
  tls.certificateChainFile = fixturePath("tls-localhost-cert.pem");
  tls.privateKeyFile = fixturePath("tls-localhost-key.pem");
  serverOptions.tls = std::move(tls);

  mcp_transport::HttpServerRuntime server(std::move(serverOptions));
  server.setRequestHandler(makeOkHandler());
  server.start();

  const mcp_transport::HttpClientRuntime client(makeClientOptions(endpointUrl("https", server.localPort())));
  REQUIRE_THROWS(client.execute(makePostRequest()));
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
