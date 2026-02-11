#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <boost/asio/connect.hpp>  // NOLINT(misc-include-cleaner)
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>  // NOLINT(misc-include-cleaner)
#include <boost/beast/core.hpp>  // NOLINT(misc-include-cleaner)
#include <boost/beast/http.hpp>  // NOLINT(misc-include-cleaner)
#include <mcp/transport/http.hpp>

// NOLINTBEGIN(misc-include-cleaner, llvm-prefer-static-over-anonymous-namespace, readability-identifier-naming,
// cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-unused-return-value, cert-err33-c, hicpp-signed-bitwise,
// google-runtime-int, modernize-use-trailing-return-type, readability-implicit-bool-conversion, performance-no-automatic-move,
// bugprone-empty-catch, cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay, modernize-return-braced-init-list)
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace mcp::transport
{
namespace
{

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

struct ParsedEndpointUrl
{
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;
  bool useTls = false;
};

auto sslErrorMessage() -> std::string
{
  const unsigned long errorCode = ERR_get_error();
  if (errorCode == 0)
  {
    return "Unknown SSL error.";
  }

  char buffer[256] = {};
  ERR_error_string_n(errorCode, buffer, sizeof(buffer));
  return std::string(buffer);
}

auto parseEndpointUrl(std::string_view endpointUrl) -> ParsedEndpointUrl
{
  if (endpointUrl.empty())
  {
    throw std::invalid_argument("HTTP endpoint URL must not be empty.");
  }

  const std::size_t schemeSeparator = endpointUrl.find("://");
  if (schemeSeparator == std::string_view::npos)
  {
    throw std::invalid_argument("HTTP endpoint URL must include an explicit scheme.");
  }

  ParsedEndpointUrl parsed;
  parsed.scheme = http::detail::toLowerAscii(endpointUrl.substr(0, schemeSeparator));
  if (parsed.scheme == "http")
  {
    parsed.useTls = false;
    parsed.port = "80";
  }
  else if (parsed.scheme == "https")
  {
    parsed.useTls = true;
    parsed.port = "443";
  }
  else
  {
    throw std::invalid_argument("HTTP endpoint URL must use http:// or https://.");
  }

  const std::size_t authorityBegin = schemeSeparator + 3;
  const std::size_t pathBegin = endpointUrl.find('/', authorityBegin);
  const std::string_view authority = pathBegin == std::string_view::npos ? endpointUrl.substr(authorityBegin) : endpointUrl.substr(authorityBegin, pathBegin - authorityBegin);
  parsed.path = pathBegin == std::string_view::npos ? "/" : std::string(endpointUrl.substr(pathBegin));
  if (parsed.path.empty())
  {
    parsed.path = "/";
  }

  if (authority.empty())
  {
    throw std::invalid_argument("HTTP endpoint URL authority must not be empty.");
  }

  if (authority.front() == '[')
  {
    const std::size_t ipv6End = authority.find(']');
    if (ipv6End == std::string_view::npos)
    {
      throw std::invalid_argument("HTTP endpoint URL contains an invalid IPv6 authority.");
    }

    parsed.host = std::string(authority.substr(1, ipv6End - 1));
    if (ipv6End + 1 < authority.size())
    {
      if (authority[ipv6End + 1] != ':')
      {
        throw std::invalid_argument("HTTP endpoint URL contains an invalid IPv6 port separator.");
      }

      parsed.port = std::string(authority.substr(ipv6End + 2));
    }
  }
  else
  {
    const std::size_t portSeparator = authority.rfind(':');
    if (portSeparator == std::string_view::npos)
    {
      parsed.host = std::string(authority);
    }
    else
    {
      parsed.host = std::string(authority.substr(0, portSeparator));
      parsed.port = std::string(authority.substr(portSeparator + 1));
    }
  }

  if (parsed.host.empty())
  {
    throw std::invalid_argument("HTTP endpoint URL host must not be empty.");
  }

  if (parsed.port.empty())
  {
    throw std::invalid_argument("HTTP endpoint URL port must not be empty when specified.");
  }

  return parsed;
}

auto hostHeaderValue(const ParsedEndpointUrl &endpoint) -> std::string
{
  std::string host = endpoint.host;
  if (host.find(':') != std::string::npos)
  {
    host = "[" + host + "]";
  }

  const bool defaultPort = (!endpoint.useTls && endpoint.port == "80") || (endpoint.useTls && endpoint.port == "443");
  if (defaultPort)
  {
    return host;
  }

  return host + ":" + endpoint.port;
}

auto configureServerTlsContext(ssl::context &context, const http::ServerTlsConfiguration &configuration) -> void
{
  if (configuration.certificateChainFile.empty() || configuration.privateKeyFile.empty())
  {
    throw std::invalid_argument("Server TLS configuration requires certificateChainFile and privateKeyFile.");
  }

  context.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);

  if (configuration.privateKeyPassphrase.has_value())
  {
    const std::string privateKeyPassphrase = *configuration.privateKeyPassphrase;
    context.set_password_callback([privateKeyPassphrase](std::size_t, ssl::context::password_purpose) -> std::string { return privateKeyPassphrase; });
  }

  context.use_certificate_chain_file(configuration.certificateChainFile);
  context.use_private_key_file(configuration.privateKeyFile, ssl::context::pem);
  context.set_verify_mode(ssl::verify_none);

  if (configuration.clientAuthenticationMode != http::TlsClientAuthenticationMode::kNone)
  {
    bool hasClientTrustRoots = false;
    if (configuration.clientCaCertificateFile.has_value() && !configuration.clientCaCertificateFile->empty())
    {
      context.load_verify_file(*configuration.clientCaCertificateFile);
      hasClientTrustRoots = true;
    }

    if (configuration.clientCaCertificatePath.has_value() && !configuration.clientCaCertificatePath->empty())
    {
      context.add_verify_path(*configuration.clientCaCertificatePath);
      hasClientTrustRoots = true;
    }

    if (!hasClientTrustRoots)
    {
      context.set_default_verify_paths();
    }

    ssl::verify_mode verifyMode = ssl::verify_peer;
    if (configuration.clientAuthenticationMode == http::TlsClientAuthenticationMode::kRequired)
    {
      verifyMode |= ssl::verify_fail_if_no_peer_cert;
    }

    context.set_verify_mode(verifyMode);
  }
}

auto configureClientTlsContext(ssl::context &context, const http::ClientTlsConfiguration &configuration) -> void
{
  context.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);

  if (!configuration.verifyPeer)
  {
    context.set_verify_mode(ssl::verify_none);
    return;
  }

  context.set_verify_mode(ssl::verify_peer);

  bool hasTrustRoots = false;
  if (configuration.caCertificateFile.has_value() && !configuration.caCertificateFile->empty())
  {
    context.load_verify_file(*configuration.caCertificateFile);
    hasTrustRoots = true;
  }

  if (configuration.caCertificatePath.has_value() && !configuration.caCertificatePath->empty())
  {
    context.add_verify_path(*configuration.caCertificatePath);
    hasTrustRoots = true;
  }

  if (!hasTrustRoots)
  {
    context.set_default_verify_paths();
  }
}

auto toBeastMethod(http::ServerRequestMethod method) -> beast_http::verb
{
  switch (method)
  {
    case http::ServerRequestMethod::kGet:
      return beast_http::verb::get;
    case http::ServerRequestMethod::kPost:
      return beast_http::verb::post;
    case http::ServerRequestMethod::kDelete:
      return beast_http::verb::delete_;
  }

  return beast_http::verb::unknown;
}

auto toServerRequestMethod(beast_http::verb method) -> std::optional<http::ServerRequestMethod>
{
  switch (method)
  {
    case beast_http::verb::get:
      return http::ServerRequestMethod::kGet;
    case beast_http::verb::post:
      return http::ServerRequestMethod::kPost;
    case beast_http::verb::delete_:
      return http::ServerRequestMethod::kDelete;
    default:
      return std::nullopt;
  }
}

template<typename HeaderFields>
auto toHeaderList(const HeaderFields &fields) -> http::HeaderList
{
  http::HeaderList headers;
  for (const auto &header : fields)
  {
    headers.push_back(http::Header {std::string(header.name_string()), std::string(header.value())});
  }

  return headers;
}

auto toBeastResponse(const beast_http::request<beast_http::string_body> &request, const http::ServerResponse &response) -> beast_http::response<beast_http::string_body>
{
  beast_http::response<beast_http::string_body> outbound {static_cast<beast_http::status>(response.statusCode), request.version()};
  outbound.keep_alive(false);

  for (const http::Header &header : response.headers)
  {
    outbound.set(header.name, header.value);
  }

  outbound.body() = response.body;
  outbound.prepare_payload();
  return outbound;
}

auto toServerResponse(const beast_http::response<beast_http::string_body> &response) -> http::ServerResponse
{
  http::ServerResponse translated;
  translated.statusCode = static_cast<std::uint16_t>(response.result_int());
  translated.headers = toHeaderList(response.base());
  translated.body = response.body();
  return translated;
}

template<typename Stream>
auto writeAndRead(Stream &stream, const beast_http::request<beast_http::string_body> &request) -> beast_http::response<beast_http::string_body>
{
  beast_http::write(stream, request);

  beast::flat_buffer buffer;
  beast_http::response<beast_http::string_body> response;
  beast_http::read(stream, buffer, response);
  return response;
}

auto shouldIgnoreTlsShutdownError(const beast::error_code &error) -> bool
{
  return error == asio::error::eof || error == asio::ssl::error::stream_truncated;
}

}  // namespace

struct HttpServerRuntime::Impl
{
  explicit Impl(HttpServerOptions options)
    : options_(std::move(options))
  {
  }

  auto setRequestHandler(HttpRequestHandler handler) -> void
  {
    const std::scoped_lock lock(mutex_);
    requestHandler_ = std::move(handler);
  }

  auto start() -> void
  {
    if (running_.exchange(true))
    {
      return;
    }

    stopRequested_.store(false);

    std::promise<std::uint16_t> startupPromise;
    auto startupFuture = startupPromise.get_future();
    serverThread_ = std::thread([this, startupPromise = std::move(startupPromise)]() mutable { run(std::move(startupPromise)); });

    try
    {
      localPort_ = startupFuture.get();
    }
    catch (...)
    {
      running_.store(false);
      stopRequested_.store(true);
      if (serverThread_.joinable())
      {
        serverThread_.join();
      }

      throw;
    }
  }

  auto stop() noexcept -> void
  {
    const bool wasRunning = running_.exchange(false);
    stopRequested_.store(true);

    if (wasRunning)
    {
      wakeAcceptLoop();
    }

    if (serverThread_.joinable())
    {
      serverThread_.join();
    }
  }

  [[nodiscard]] auto isRunning() const noexcept -> bool { return running_.load(); }

  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t { return localPort_; }

  auto run(std::promise<std::uint16_t> startupPromise) -> void
  {
    bool startupSignaled = false;

    try
    {
      asio::io_context ioContext {1};
      tcp::acceptor acceptor {ioContext};

      beast::error_code error;
      acceptor.open(tcp::v4(), error);
      if (error)
      {
        throw std::runtime_error("HTTP server failed to open listen socket.");
      }

      const auto bindAddress = options_.endpoint.bindLocalhostOnly ? asio::ip::address_v4::loopback() : asio::ip::make_address_v4(options_.endpoint.bindAddress);
      acceptor.bind(tcp::endpoint {bindAddress, options_.endpoint.port}, error);
      if (error)
      {
        throw std::runtime_error("HTTP server failed to bind listen socket.");
      }

      acceptor.listen(asio::socket_base::max_listen_connections, error);
      if (error)
      {
        throw std::runtime_error("HTTP server failed to enter listen state.");
      }

      localPort_ = acceptor.local_endpoint(error).port();
      if (error || localPort_ == 0)
      {
        throw std::runtime_error("HTTP server failed to resolve bound local port.");
      }

      std::optional<ssl::context> tlsContext;
      if (options_.tls.has_value())
      {
        tlsContext.emplace(ssl::context::tls_server);
        configureServerTlsContext(*tlsContext, *options_.tls);
      }

      startupPromise.set_value(localPort_);
      startupSignaled = true;

      while (!stopRequested_.load())
      {
        tcp::socket socket {ioContext};
        acceptor.accept(socket, error);
        if (error)
        {
          continue;
        }

        if (stopRequested_.load())
        {
          break;
        }

        if (tlsContext.has_value())
        {
          handleTlsConnection(std::move(socket), *tlsContext);
        }
        else
        {
          handlePlainConnection(std::move(socket));
        }
      }
    }
    catch (...)
    {
      if (!startupSignaled)
      {
        startupPromise.set_exception(std::current_exception());
      }
    }

    running_.store(false);
  }

  auto dispatch(const beast_http::request<beast_http::string_body> &request) -> http::ServerResponse
  {
    const std::optional<http::ServerRequestMethod> method = toServerRequestMethod(request.method());
    if (!method.has_value())
    {
      http::ServerResponse methodNotAllowed;
      methodNotAllowed.statusCode = static_cast<std::uint16_t>(beast_http::status::method_not_allowed);
      return methodNotAllowed;
    }

    http::ServerRequest translated;
    translated.method = *method;
    translated.path = std::string(request.target());
    translated.headers = toHeaderList(request.base());
    translated.body = request.body();

    HttpRequestHandler handler;
    {
      const std::scoped_lock lock(mutex_);
      handler = requestHandler_;
    }

    if (!handler)
    {
      http::ServerResponse unavailable;
      unavailable.statusCode = static_cast<std::uint16_t>(beast_http::status::service_unavailable);
      unavailable.body = "No HTTP request handler configured.";
      http::setHeader(unavailable.headers, http::kHeaderContentType, "text/plain");
      return unavailable;
    }

    return handler(translated);
  }

  auto handlePlainConnection(tcp::socket socket) -> void
  {
    beast::flat_buffer buffer;
    beast_http::request<beast_http::string_body> request;

    beast::error_code error;
    beast_http::read(socket, buffer, request, error);
    if (error)
    {
      return;
    }

    const beast_http::response<beast_http::string_body> response = toBeastResponse(request, dispatch(request));
    beast_http::write(socket, response, error);

    socket.shutdown(tcp::socket::shutdown_both, error);
  }

  auto handleTlsConnection(tcp::socket socket, ssl::context &context) -> void
  {
    ssl::stream<tcp::socket> stream(std::move(socket), context);

    beast::error_code error;
    stream.handshake(ssl::stream_base::server, error);
    if (error)
    {
      return;
    }

    beast::flat_buffer buffer;
    beast_http::request<beast_http::string_body> request;
    beast_http::read(stream, buffer, request, error);
    if (error)
    {
      return;
    }

    const beast_http::response<beast_http::string_body> response = toBeastResponse(request, dispatch(request));
    beast_http::write(stream, response, error);

    stream.shutdown(error);
  }

  auto wakeAcceptLoop() const noexcept -> void
  {
    if (localPort_ == 0)
    {
      return;
    }

    try
    {
      asio::io_context ioContext {1};
      tcp::socket socket {ioContext};
      beast::error_code error;
      socket.connect({asio::ip::address_v4::loopback(), localPort_}, error);
    }
    catch (...)
    {
    }
  }

  HttpServerOptions options_;
  HttpRequestHandler requestHandler_;
  std::thread serverThread_;
  std::atomic<bool> stopRequested_ {false};
  std::atomic<bool> running_ {false};
  mutable std::mutex mutex_;
  std::uint16_t localPort_ = 0;
};

struct HttpClientRuntime::Impl
{
  explicit Impl(HttpClientOptions options)
    : options_(std::move(options))
    , endpoint_(parseEndpointUrl(options_.endpointUrl))
  {
    if (options_.endpointUrl.empty())
    {
      throw std::invalid_argument("HttpClientOptions.endpointUrl must not be empty.");
    }
  }

  [[nodiscard]] auto execute(const http::ServerRequest &request) const -> http::ServerResponse
  {
    if (endpoint_.useTls)
    {
      return executeTls(request);
    }

    return executePlain(request);
  }

  [[nodiscard]] auto buildRequest(const http::ServerRequest &request) const -> beast_http::request<beast_http::string_body>
  {
    std::string target = request.path.empty() ? endpoint_.path : request.path;
    if (target.empty())
    {
      target = "/";
    }

    if (!target.empty() && target.front() != '/')
    {
      target.insert(target.begin(), '/');
    }

    beast_http::request<beast_http::string_body> outbound {toBeastMethod(request.method), target, 11};
    outbound.body() = request.body;

    for (const http::Header &header : request.headers)
    {
      outbound.set(header.name, header.value);
    }

    if (!outbound.base().count(beast_http::field::host))
    {
      outbound.set(beast_http::field::host, hostHeaderValue(endpoint_));
    }

    if (options_.bearerToken.has_value() && !options_.bearerToken->empty() && !outbound.base().count(beast_http::field::authorization))
    {
      outbound.set(beast_http::field::authorization, "Bearer " + *options_.bearerToken);
    }

    outbound.prepare_payload();
    return outbound;
  }

  [[nodiscard]] auto executePlain(const http::ServerRequest &request) const -> http::ServerResponse
  {
    asio::io_context ioContext {1};
    tcp::resolver resolver {ioContext};
    const auto endpoints = resolver.resolve(endpoint_.host, endpoint_.port);

    tcp::socket socket {ioContext};
    asio::connect(socket, endpoints);

    const beast_http::response<beast_http::string_body> response = writeAndRead(socket, buildRequest(request));

    beast::error_code error;
    socket.shutdown(tcp::socket::shutdown_both, error);
    return toServerResponse(response);
  }

  [[nodiscard]] auto executeTls(const http::ServerRequest &request) const -> http::ServerResponse
  {
    asio::io_context ioContext {1};
    tcp::resolver resolver {ioContext};
    const auto endpoints = resolver.resolve(endpoint_.host, endpoint_.port);

    ssl::context context(ssl::context::tls_client);
    configureClientTlsContext(context, options_.tls);

    ssl::stream<tcp::socket> stream(ioContext, context);

    const std::string sniHost = options_.tls.serverNameIndication.has_value() && !options_.tls.serverNameIndication->empty() ? *options_.tls.serverNameIndication : endpoint_.host;
    if (!sniHost.empty())
    {
      if (!SSL_set_tlsext_host_name(stream.native_handle(), sniHost.c_str()))
      {
        throw std::runtime_error("Failed to configure TLS SNI host: " + sslErrorMessage());
      }

      if (options_.tls.verifyPeer)
      {
        stream.set_verify_callback(ssl::host_name_verification(sniHost));
      }
    }

    asio::connect(stream.next_layer(), endpoints);
    stream.handshake(ssl::stream_base::client);

    const beast_http::response<beast_http::string_body> response = writeAndRead(stream, buildRequest(request));

    beast::error_code error;
    stream.shutdown(error);
    if (error && !shouldIgnoreTlsShutdownError(error))
    {
      throw beast::system_error(error);
    }

    return toServerResponse(response);
  }

  HttpClientOptions options_;
  ParsedEndpointUrl endpoint_;
};

HttpServerRuntime::HttpServerRuntime(HttpServerOptions options)
  : impl_(std::make_unique<Impl>(std::move(options)))
{
}

HttpServerRuntime::~HttpServerRuntime()
{
  impl_->stop();
}

HttpServerRuntime::HttpServerRuntime(HttpServerRuntime &&other) noexcept = default;

auto HttpServerRuntime::operator=(HttpServerRuntime &&other) noexcept -> HttpServerRuntime & = default;

auto HttpServerRuntime::setRequestHandler(HttpRequestHandler handler) -> void
{
  impl_->setRequestHandler(std::move(handler));
}

auto HttpServerRuntime::start() -> void
{
  impl_->start();
}

auto HttpServerRuntime::stop() noexcept -> void
{
  impl_->stop();
}

auto HttpServerRuntime::isRunning() const noexcept -> bool
{
  return impl_->isRunning();
}

auto HttpServerRuntime::localPort() const noexcept -> std::uint16_t
{
  return impl_->localPort();
}

HttpClientRuntime::HttpClientRuntime(HttpClientOptions options)
  : impl_(std::make_unique<Impl>(std::move(options)))
{
}

HttpClientRuntime::~HttpClientRuntime() = default;

HttpClientRuntime::HttpClientRuntime(HttpClientRuntime &&other) noexcept = default;

auto HttpClientRuntime::operator=(HttpClientRuntime &&other) noexcept -> HttpClientRuntime & = default;

auto HttpClientRuntime::execute(const http::ServerRequest &request) const -> http::ServerResponse
{
  return impl_->execute(request);
}

}  // namespace mcp::transport

// NOLINTEND(misc-include-cleaner, llvm-prefer-static-over-anonymous-namespace, readability-identifier-naming,
// cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-unused-return-value, cert-err33-c, hicpp-signed-bitwise,
// google-runtime-int, modernize-use-trailing-return-type, readability-implicit-bool-conversion, performance-no-automatic-move,
// bugprone-empty-catch, cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay, modernize-return-braced-init-list)
