#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/transport/all.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace
{

namespace mcp_transport = mcp::transport;
namespace mcp_http = mcp::transport::http;

auto makeClientOptions(std::string endpointUrl) -> mcp_transport::http::HttpClientOptions
{
  mcp_transport::http::HttpClientOptions options;
  options.endpointUrl = std::move(endpointUrl);
  return options;
}

auto makeServerOptions() -> mcp_transport::http::HttpServerOptions
{
  mcp_transport::http::HttpServerOptions options;
  options.endpoint.path = "/mcp";
  options.endpoint.bindAddress = "127.0.0.1";
  options.endpoint.bindLocalhostOnly = true;
  options.endpoint.port = 0;
  return options;
}

}  // namespace

TEST_CASE("HTTP server runtime basic operations", "[transport][http][runtime]")
{
  // Placeholder test - will be expanded in future tasks
  REQUIRE(true);
}

TEST_CASE("HttpClientRuntime rejects invalid endpoint URLs", "[transport][http][runtime][url]")
{
  SECTION("empty URL throws std::invalid_argument")
  {
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("")), std::invalid_argument);
  }

  SECTION("missing scheme throws std::invalid_argument")
  {
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("localhost:8080/mcp")), std::invalid_argument);
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("//localhost:8080/mcp")), std::invalid_argument);
  }

  SECTION("unsupported scheme throws std::invalid_argument")
  {
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("ftp://localhost:8080/mcp")), std::invalid_argument);
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("ws://localhost:8080/mcp")), std::invalid_argument);
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("file:///tmp/mcp")), std::invalid_argument);
  }

  SECTION("empty host throws std::invalid_argument")
  {
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("http://:8080/mcp")), std::invalid_argument);
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("http:///mcp")), std::invalid_argument);
  }

  SECTION("userinfo in authority throws std::invalid_argument")
  {
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("http://user@localhost/mcp")), std::invalid_argument);
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("https://user:pass@localhost/mcp")), std::invalid_argument);
  }

  SECTION("invalid IPv6 authority throws std::invalid_argument")
  {
    // Missing closing bracket
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("http://[::1:8080/mcp")), std::invalid_argument);

    // Missing colon before port after IPv6 address (e.g., "[::1]8080" instead of "[::1]:8080")
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("http://[::1]8080/mcp")), std::invalid_argument);

    // Invalid character after IPv6 address (anything other than ':' or '/')
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("http://[::1]-8080/mcp")), std::invalid_argument);

    // Only opening bracket without content
    REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(makeClientOptions("http://[")), std::invalid_argument);  // NOLINT(bugprone-throw-keyword-missing)
  }
}

TEST_CASE("HttpClientRuntime normalizes endpoint request target and strips fragments", "[transport][http][runtime][url]")
{
  mcp_transport::http::HttpServerRuntime server(makeServerOptions());

  std::mutex observedMutex;
  std::optional<std::string> observedPath;
  server.setRequestHandler(
    [&observedMutex, &observedPath](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
    {
      {
        const std::scoped_lock lock(observedMutex);
        observedPath = request.path;
      }

      mcp_http::ServerResponse response;
      response.statusCode = 200;
      response.body = "ok";
      return response;
    });
  server.start();

  const std::string endpoint = "http://127.0.0.1:" + std::to_string(server.localPort()) + "?mode=test#fragment";
  const mcp_transport::http::HttpClientRuntime client(makeClientOptions(endpoint));

  mcp_http::ServerRequest request;
  request.method = mcp_http::ServerRequestMethod::kPost;
  request.path.clear();
  request.body = "{}";

  const mcp_http::ServerResponse response = client.execute(request);
  REQUIRE(response.statusCode == 200);

  {
    const std::scoped_lock lock(observedMutex);
    REQUIRE(observedPath.has_value());
    REQUIRE(*observedPath == "/?mode=test");
  }

  server.stop();
}

#if !MCP_SDK_ENABLE_TLS
TEST_CASE("HttpClientRuntime rejects HTTPS endpoint when TLS is disabled", "[transport][http][runtime][tls]")
{
  const auto options = makeClientOptions("https://localhost:8443/mcp");
  REQUIRE_THROWS_AS(mcp_transport::http::HttpClientRuntime(options), std::invalid_argument);
}

TEST_CASE("HttpServerRuntime rejects TLS configuration when TLS is disabled", "[transport][http][runtime][tls]")
{
  mcp_transport::http::HttpServerOptions options = makeServerOptions();
  mcp_http::ServerTlsConfiguration tls;
  tls.certificateChainFile = "/path/to/cert.pem";
  tls.privateKeyFile = "/path/to/key.pem";
  options.tls = std::move(tls);

  mcp_transport::http::HttpServerRuntime server(std::move(options));
  // The TLS validation happens at start() time, not construction
  REQUIRE_THROWS_AS(server.start(), std::invalid_argument);
}
#endif

TEST_CASE("HttpServerRuntime rejects incomplete TLS configuration", "[transport][http][runtime][tls]")
{
#if MCP_SDK_ENABLE_TLS
  SECTION("missing certificate file throws std::invalid_argument")
  {
    mcp_transport::http::HttpServerOptions options = makeServerOptions();
    mcp_http::ServerTlsConfiguration tls;
    tls.certificateChainFile = "";
    tls.privateKeyFile = "/path/to/key.pem";
    options.tls = std::move(tls);

    mcp_transport::http::HttpServerRuntime server(std::move(options));
    REQUIRE_THROWS_AS(server.start(), std::invalid_argument);
  }

  SECTION("missing private key file throws std::invalid_argument")
  {
    mcp_transport::http::HttpServerOptions options = makeServerOptions();
    mcp_http::ServerTlsConfiguration tls;
    tls.certificateChainFile = "/path/to/cert.pem";
    tls.privateKeyFile = "";
    options.tls = std::move(tls);

    mcp_transport::http::HttpServerRuntime server(std::move(options));
    REQUIRE_THROWS_AS(server.start(), std::invalid_argument);
  }

  SECTION("both missing throws std::invalid_argument")
  {
    mcp_transport::http::HttpServerOptions options = makeServerOptions();
    mcp_http::ServerTlsConfiguration tls;
    tls.certificateChainFile = "";
    tls.privateKeyFile = "";
    options.tls = std::move(tls);

    mcp_transport::http::HttpServerRuntime server(std::move(options));
    REQUIRE_THROWS_AS(server.start(), std::invalid_argument);
  }
#endif
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
