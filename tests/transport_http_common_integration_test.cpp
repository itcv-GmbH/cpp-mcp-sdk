#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>  // NOLINT(misc-include-cleaner)
#include <boost/beast/http.hpp>  // NOLINT(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/sdk/version.hpp>

// NOLINTBEGIN(misc-include-cleaner, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
// misc-const-correctness, llvm-prefer-static-over-anonymous-namespace)

namespace
{

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
namespace mcp_http = mcp::transport::http;

class LocalValidationHttpServer
{
public:
  explicit LocalValidationHttpServer(mcp_http::RequestValidationOptions options)
    : options_(std::move(options))
  {
    auto readyFuture = readyPromise_.get_future();

    serverThread_ = std::thread(
      [this]() -> void
      {
        try
        {
          run();
        }
        catch (...)
        {
          signalStartupFailure(std::current_exception());
        }
      });

    if (readyFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
    {
      stop();
      throw std::runtime_error("Timed out waiting for local HTTP test server startup");
    }

    // Propagate any startup failure.
    port_ = readyFuture.get();
  }

  ~LocalValidationHttpServer() { stop(); }

  LocalValidationHttpServer(const LocalValidationHttpServer &) = delete;
  auto operator=(const LocalValidationHttpServer &) -> LocalValidationHttpServer & = delete;

  [[nodiscard]] auto port() const noexcept -> std::uint16_t { return port_; }

  auto stop() noexcept -> void
  {
    const bool alreadyStopping = stopRequested_.exchange(true);
    if (alreadyStopping)
    {
      if (serverThread_.joinable())
      {
        serverThread_.join();
      }
      return;
    }

    wakeAcceptLoop();

    if (serverThread_.joinable())
    {
      serverThread_.join();
    }
  }

private:
  auto signalStartupFailure(std::exception_ptr exception) noexcept -> void
  {
    bool expected = false;
    if (startupSignaled_.compare_exchange_strong(expected, true))
    {
      readyPromise_.set_exception(std::move(exception));
    }
  }

  auto signalStartupSuccess(std::uint16_t port) -> void
  {
    bool expected = false;
    if (startupSignaled_.compare_exchange_strong(expected, true))
    {
      readyPromise_.set_value(port);
    }
  }

  auto run() -> void
  {
    asio::io_context ioContext {1};
    asio::ip::tcp::acceptor acceptor {ioContext};

    beast::error_code error;
    acceptor.open(asio::ip::tcp::v4(), error);
    if (error)
    {
      throw std::runtime_error("Failed to open acceptor");
    }

    acceptor.bind({asio::ip::address_v4::loopback(), 0}, error);
    if (error)
    {
      throw std::runtime_error("Failed to bind acceptor");
    }

    acceptor.listen(asio::socket_base::max_listen_connections, error);
    if (error)
    {
      throw std::runtime_error("Failed to listen on acceptor");
    }

    const std::uint16_t boundPort = acceptor.local_endpoint(error).port();
    if (error || boundPort == 0)
    {
      throw std::runtime_error("Failed to resolve test server port");
    }

    signalStartupSuccess(boundPort);

    while (!stopRequested_.load())
    {
      asio::ip::tcp::socket socket {ioContext};
      acceptor.accept(socket, error);
      if (error)
      {
        continue;
      }

      if (stopRequested_.load())
      {
        break;
      }

      handleConnection(std::move(socket));
    }
  }

  static auto toHeaders(const beast_http::request<beast_http::string_body> &request) -> mcp_http::HeaderList
  {
    mcp_http::HeaderList headers;
    for (const auto &header : request.base())
    {
      headers.push_back(mcp_http::Header {std::string(header.name_string()), std::string(header.value())});
    }

    return headers;
  }

  auto handleConnection(asio::ip::tcp::socket socket) const -> void
  {
    beast::flat_buffer buffer;
    beast_http::request<beast_http::string_body> request;

    beast::error_code error;
    beast_http::read(socket, buffer, request, error);
    if (error)
    {
      return;
    }

    const auto validationResult = mcp_http::validateServerRequest(toHeaders(request), options_);

    const auto status = validationResult.accepted ? beast_http::status::ok : static_cast<beast_http::status>(validationResult.statusCode);
    beast_http::response<beast_http::string_body> response {status, request.version()};
    response.set(beast_http::field::content_type, "text/plain");
    response.body() = validationResult.reason;
    response.prepare_payload();

    beast_http::write(socket, response, error);
    static_cast<void>(error);

    socket.shutdown(asio::ip::tcp::socket::shutdown_both, error);
  }

  auto wakeAcceptLoop() const noexcept -> void
  {
    if (port_ == 0)
    {
      return;
    }

    try
    {
      asio::io_context ioContext {1};
      asio::ip::tcp::socket socket {ioContext};
      beast::error_code error;
      socket.connect({asio::ip::address_v4::loopback(), port_}, error);
    }
    catch (...)
    {
    }
  }

  mcp_http::RequestValidationOptions options_;
  std::thread serverThread_;
  std::promise<std::uint16_t> readyPromise_;
  std::atomic<bool> startupSignaled_ {false};
  std::atomic<bool> stopRequested_ {false};
  std::uint16_t port_ = 0;
};

auto sendValidationRequest(std::uint16_t port, std::optional<std::string_view> origin, std::optional<std::string_view> protocolVersion, std::optional<std::string_view> sessionId)
  -> beast_http::status
{
  asio::io_context ioContext {1};
  beast::tcp_stream stream {ioContext};
  stream.expires_after(std::chrono::seconds(2));

  stream.connect({asio::ip::address_v4::loopback(), port});

  beast_http::request<beast_http::string_body> request {beast_http::verb::post, "/mcp", 11};
  request.set(beast_http::field::host, "127.0.0.1");

  if (origin.has_value())
  {
    request.set(beast_http::field::origin, *origin);
  }

  if (protocolVersion.has_value())
  {
    request.set("MCP-Protocol-Version", *protocolVersion);
  }

  if (sessionId.has_value())
  {
    request.set("MCP-Session-Id", *sessionId);
  }

  request.body() = "{}";
  request.prepare_payload();

  beast_http::write(stream, request);

  beast::flat_buffer buffer;
  beast_http::response<beast_http::string_body> response;
  beast_http::read(stream, buffer, response);

  beast::error_code error;
  stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, error);
  return response.result();
}

}  // namespace

TEST_CASE("HTTP integration: invalid or unsupported MCP-Protocol-Version returns 400", "[transport][http][integration]")
{
  mcp_http::RequestValidationOptions options;
  options.supportedProtocolVersions = {std::string(mcp::kLatestProtocolVersion)};

  LocalValidationHttpServer server(std::move(options));

  SECTION("Invalid format")
  {
    const auto status = sendValidationRequest(server.port(), "http://localhost:6274", "2025/11/25", std::nullopt);
    REQUIRE(status == beast_http::status::bad_request);
  }

  SECTION("Unsupported version")
  {
    const auto status = sendValidationRequest(server.port(), "http://localhost:6274", "1900-01-01", std::nullopt);
    REQUIRE(status == beast_http::status::bad_request);
  }
}

TEST_CASE("HTTP integration: invalid Origin returns 403", "[transport][http][integration]")
{
  LocalValidationHttpServer server(mcp_http::RequestValidationOptions {});

  const auto status = sendValidationRequest(server.port(), "https://evil.example", std::string(mcp::kLatestProtocolVersion), std::nullopt);
  REQUIRE(status == beast_http::status::forbidden);
}

TEST_CASE("HTTP integration: required MCP-Session-Id missing returns 400", "[transport][http][integration]")
{
  mcp_http::RequestValidationOptions options;
  options.sessionRequired = true;
  options.requestKind = mcp_http::RequestKind::kOther;

  LocalValidationHttpServer server(std::move(options));

  const auto status = sendValidationRequest(server.port(), "http://localhost:6274", std::string(mcp::kLatestProtocolVersion), std::nullopt);
  REQUIRE(status == beast_http::status::bad_request);
}

TEST_CASE("HTTP integration: terminated or expired session returns 404", "[transport][http][integration]")
{
  SECTION("Expired session")
  {
    mcp_http::RequestValidationOptions options;
    options.sessionRequired = true;
    options.requestKind = mcp_http::RequestKind::kOther;
    options.sessionResolver = [](std::string_view) -> mcp_http::SessionResolution
    {
      return {
        mcp_http::SessionLookupState::kExpired,
        std::nullopt,
      };
    };

    LocalValidationHttpServer server(std::move(options));
    const auto status = sendValidationRequest(server.port(), "http://localhost:6274", std::string(mcp::kLatestProtocolVersion), "session-123");
    REQUIRE(status == beast_http::status::not_found);
  }

  SECTION("Terminated session")
  {
    mcp_http::RequestValidationOptions options;
    options.sessionRequired = true;
    options.requestKind = mcp_http::RequestKind::kOther;
    options.sessionResolver = [](std::string_view) -> mcp_http::SessionResolution
    {
      return {
        mcp_http::SessionLookupState::kTerminated,
        std::nullopt,
      };
    };

    LocalValidationHttpServer server(std::move(options));
    const auto status = sendValidationRequest(server.port(), "http://localhost:6274", std::string(mcp::kLatestProtocolVersion), "session-123");
    REQUIRE(status == beast_http::status::not_found);
  }
}

// NOLINTEND(misc-include-cleaner, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
// misc-const-correctness, llvm-prefer-static-over-anonymous-namespace)
