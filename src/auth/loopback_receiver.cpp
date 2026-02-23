#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <mcp/auth/all.hpp>
#include <mcp/detail/ascii.hpp>
#include <mcp/transport/http.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
// hicpp-signed-bitwise, misc-const-correctness, misc-include-cleaner)

namespace mcp::auth
{
namespace
{

constexpr std::uint16_t kHttpStatusOk = 200;
constexpr std::uint16_t kHttpStatusBadRequest = 400;
constexpr std::uint16_t kHttpStatusNotFound = 404;
constexpr std::uint16_t kHttpStatusMethodNotAllowed = 405;

auto normalizeCallbackPath(std::string_view path) -> std::string
{
  const std::string_view trimmed = mcp::detail::trimAsciiWhitespace(path);
  if (trimmed.empty())
  {
    throw LoopbackReceiverError(LoopbackReceiverErrorCode::kInvalidInput, "Loopback callbackPath must not be empty");
  }

  if (trimmed.find('?') != std::string_view::npos || trimmed.find('#') != std::string_view::npos)
  {
    throw LoopbackReceiverError(LoopbackReceiverErrorCode::kInvalidInput, "Loopback callbackPath must not contain query or fragment components");
  }

  std::string normalized(trimmed);
  if (normalized.front() != '/')
  {
    normalized.insert(normalized.begin(), '/');
  }

  while (normalized.size() > 1 && normalized.back() == '/')
  {
    normalized.pop_back();
  }

  return normalized;
}

auto decodeQueryComponent(std::string_view encoded) -> std::string
{
  auto decodeNibble = [](char value) -> std::optional<std::uint8_t>
  {
    if (value >= '0' && value <= '9')
    {
      return static_cast<std::uint8_t>(value - '0');
    }

    if (value >= 'A' && value <= 'F')
    {
      return static_cast<std::uint8_t>(10 + (value - 'A'));
    }

    if (value >= 'a' && value <= 'f')
    {
      return static_cast<std::uint8_t>(10 + (value - 'a'));
    }

    return std::nullopt;
  };

  std::string decoded;
  decoded.reserve(encoded.size());

  for (std::size_t index = 0; index < encoded.size(); ++index)
  {
    const char character = encoded[index];
    if (character == '+')
    {
      decoded.push_back(' ');
      continue;
    }

    if (character != '%')
    {
      decoded.push_back(character);
      continue;
    }

    if (index + 2 >= encoded.size())
    {
      throw LoopbackReceiverError(LoopbackReceiverErrorCode::kProtocolViolation, "Loopback callback query includes an invalid percent-encoding sequence");
    }

    const auto high = decodeNibble(encoded[index + 1]);
    const auto low = decodeNibble(encoded[index + 2]);
    if (!high.has_value() || !low.has_value())
    {
      throw LoopbackReceiverError(LoopbackReceiverErrorCode::kProtocolViolation, "Loopback callback query includes a non-hex percent-encoding sequence");
    }

    decoded.push_back(static_cast<char>(((*high) << 4U) | *low));
    index += 2;
  }

  return decoded;
}

auto parseQueryParameterValue(std::string_view query, std::string_view key) -> std::optional<std::string>
{
  std::size_t begin = 0;
  while (begin <= query.size())
  {
    std::size_t end = query.find('&', begin);
    if (end == std::string_view::npos)
    {
      end = query.size();
    }

    const std::string_view pair = query.substr(begin, end - begin);
    if (!pair.empty())
    {
      const std::size_t separator = pair.find('=');
      const std::string_view encodedName = separator == std::string_view::npos ? pair : pair.substr(0, separator);
      const std::string_view encodedValue = separator == std::string_view::npos ? std::string_view {} : pair.substr(separator + 1);

      const std::string decodedName = decodeQueryComponent(encodedName);
      if (decodedName == key)
      {
        return decodeQueryComponent(encodedValue);
      }
    }

    if (end == query.size())
    {
      break;
    }

    begin = end + 1;
  }

  return std::nullopt;
}

auto splitRequestTarget(std::string_view target) -> std::pair<std::string, std::string>
{
  if (target.empty())
  {
    return {"/", ""};
  }

  const std::size_t querySeparator = target.find('?');
  if (querySeparator == std::string_view::npos)
  {
    return {normalizeCallbackPath(target), ""};
  }

  const std::string_view rawPath = target.substr(0, querySeparator);
  const std::string_view query = target.substr(querySeparator + 1);
  return {normalizeCallbackPath(rawPath.empty() ? "/" : rawPath), std::string(query)};
}

auto defaultSuccessHtml() -> std::string
{
  return "<!doctype html><html><head><meta charset=\"utf-8\"><title>Authorization complete</title></head><body><h1>Authorization complete</h1><p>You can return to the "
         "application.</p></body></html>";
}

auto htmlResponse(std::uint16_t statusCode, std::string body) -> transport::http::ServerResponse
{
  transport::http::ServerResponse response;
  response.statusCode = statusCode;
  response.body = std::move(body);
  transport::http::setHeader(response.headers, transport::http::kHeaderContentType, "text/html; charset=utf-8");
  return response;
}

}  // namespace

class LoopbackRedirectReceiver::Impl
{
public:
  explicit Impl(LoopbackReceiverOptions options)
    : options_(std::move(options))
  {
    options_.callbackPath = normalizeCallbackPath(options_.callbackPath);
    if (options_.authorizationTimeout <= std::chrono::milliseconds::zero())
    {
      throw LoopbackReceiverError(LoopbackReceiverErrorCode::kInvalidInput, "Loopback authorizationTimeout must be greater than zero");
    }

    if (!options_.successHtml.has_value())
    {
      options_.successHtml = defaultSuccessHtml();
    }
  }

  auto start() -> void
  {
    std::scoped_lock lock(mutex_);
    if (runtime_ && runtime_->isRunning())
    {
      return;
    }

    expectedState_.reset();
    pendingAuthorizationCode_.reset();
    completionSignaled_ = false;
    resultConsumed_ = false;
    resultPromise_ = std::promise<LoopbackAuthorizationCode>();
    resultFuture_ = resultPromise_.get_future();

    transport::HttpServerOptions httpOptions;
    httpOptions.endpoint.path = options_.callbackPath;
    httpOptions.endpoint.bindAddress = "127.0.0.1";
    httpOptions.endpoint.port = 0;
    httpOptions.endpoint.bindLocalhostOnly = true;

    runtime_ = std::make_unique<transport::HttpServerRuntime>(std::move(httpOptions));
    runtime_->setRequestHandler([this](const transport::http::ServerRequest &request) -> transport::http::ServerResponse { return handleRequest(request); });

    try
    {
      runtime_->start();
    }
    catch (const std::exception &error)
    {
      runtime_.reset();
      throw LoopbackReceiverError(LoopbackReceiverErrorCode::kNetworkFailure, "Failed to start loopback redirect receiver: " + std::string(error.what()));
    }
  }

  auto stop() noexcept -> void
  {
    std::unique_ptr<transport::HttpServerRuntime> runtime;
    {
      std::scoped_lock lock(mutex_);
      runtime = std::move(runtime_);
    }

    if (runtime)
    {
      runtime->stop();
    }
  }

  [[nodiscard]] auto isRunning() const noexcept -> bool
  {
    std::scoped_lock lock(mutex_);
    return runtime_ && runtime_->isRunning();
  }

  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t
  {
    std::scoped_lock lock(mutex_);
    if (!runtime_)
    {
      return 0;
    }

    return runtime_->localPort();
  }

  [[nodiscard]] auto callbackPath() const noexcept -> const std::string & { return options_.callbackPath; }

  [[nodiscard]] auto redirectUri() const -> std::string
  {
    const std::uint16_t port = localPort();
    if (port == 0)
    {
      throw LoopbackReceiverError(LoopbackReceiverErrorCode::kNetworkFailure, "Loopback redirect receiver is not running");
    }

    return "http://localhost:" + std::to_string(port) + options_.callbackPath;
  }

  auto waitForAuthorizationCode(std::string expectedState, std::optional<std::chrono::milliseconds> timeoutOverride) -> LoopbackAuthorizationCode
  {
    std::future<LoopbackAuthorizationCode> *future = nullptr;
    std::chrono::milliseconds timeout = options_.authorizationTimeout;

    {
      std::scoped_lock lock(mutex_);
      if (!runtime_ || !runtime_->isRunning())
      {
        throw LoopbackReceiverError(LoopbackReceiverErrorCode::kNetworkFailure, "Loopback redirect receiver must be started before waiting for an authorization code");
      }

      if (resultConsumed_)
      {
        throw LoopbackReceiverError(LoopbackReceiverErrorCode::kInvalidInput, "waitForAuthorizationCode can only be called once per receiver start");
      }

      const std::string_view trimmedState = mcp::detail::trimAsciiWhitespace(expectedState);
      if (trimmedState.empty())
      {
        throw LoopbackReceiverError(LoopbackReceiverErrorCode::kInvalidInput, "expectedState must not be empty");
      }

      expectedState_ = std::string(trimmedState);
      if (pendingAuthorizationCode_.has_value() && !completionSignaled_)
      {
        finalizeAuthorizationCodeLocked(*pendingAuthorizationCode_);
        pendingAuthorizationCode_.reset();
      }

      if (timeoutOverride.has_value())
      {
        timeout = *timeoutOverride;
      }

      if (timeout <= std::chrono::milliseconds::zero())
      {
        throw LoopbackReceiverError(LoopbackReceiverErrorCode::kInvalidInput, "timeoutOverride must be greater than zero");
      }

      future = &resultFuture_;
    }

    if (future->wait_for(timeout) != std::future_status::ready)
    {
      throw LoopbackReceiverError(LoopbackReceiverErrorCode::kTimeout, "Timed out waiting for OAuth authorization callback");
    }

    try
    {
      LoopbackAuthorizationCode result = future->get();
      std::scoped_lock lock(mutex_);
      resultConsumed_ = true;
      return result;
    }
    catch (const LoopbackReceiverError &)
    {
      std::scoped_lock lock(mutex_);
      resultConsumed_ = true;
      throw;
    }
  }

  auto handleRequest(const transport::http::ServerRequest &request) -> transport::http::ServerResponse
  {
    if (request.method != transport::http::ServerRequestMethod::kGet)
    {
      return htmlResponse(kHttpStatusMethodNotAllowed, "<html><body><h1>Method not allowed</h1></body></html>");
    }

    std::string requestPath;
    std::string query;
    try
    {
      const auto [parsedPath, parsedQuery] = splitRequestTarget(request.path);
      requestPath = parsedPath;
      query = parsedQuery;
    }
    catch (const LoopbackReceiverError &error)
    {
      return htmlResponse(kHttpStatusBadRequest, std::string("<html><body><h1>Invalid callback URL</h1><p>") + error.what() + "</p></body></html>");
    }

    if (requestPath != options_.callbackPath)
    {
      return htmlResponse(kHttpStatusNotFound, "<html><body><h1>Not found</h1></body></html>");
    }

    std::optional<std::string> code;
    std::optional<std::string> state;
    try
    {
      code = parseQueryParameterValue(query, "code");
      state = parseQueryParameterValue(query, "state");
    }
    catch (const LoopbackReceiverError &error)
    {
      signalFailure(LoopbackReceiverErrorCode::kProtocolViolation, std::string("Invalid callback query encoding: ") + error.what());
      return htmlResponse(kHttpStatusBadRequest, "<html><body><h1>Invalid callback query encoding</h1></body></html>");
    }

    if (!code.has_value() || !state.has_value() || mcp::detail::trimAsciiWhitespace(*code).empty())
    {
      signalFailure(LoopbackReceiverErrorCode::kProtocolViolation, "OAuth callback did not include required code/state parameters");
      return htmlResponse(kHttpStatusBadRequest, "<html><body><h1>Missing code/state callback parameters</h1></body></html>");
    }

    LoopbackAuthorizationCode authorizationCode;
    authorizationCode.code = *code;
    authorizationCode.state = *state;

    const bool stateMismatch = processAuthorizationCode(authorizationCode);
    if (stateMismatch)
    {
      return htmlResponse(kHttpStatusBadRequest, "<html><body><h1>State mismatch</h1><p>Authorization aborted for security reasons.</p></body></html>");
    }

    return htmlResponse(kHttpStatusOk, options_.successHtml.value_or(defaultSuccessHtml()));
  }

  auto processAuthorizationCode(const LoopbackAuthorizationCode &authorizationCode) -> bool
  {
    std::scoped_lock lock(mutex_);
    if (completionSignaled_)
    {
      return false;
    }

    if (!expectedState_.has_value())
    {
      pendingAuthorizationCode_ = authorizationCode;
      return false;
    }

    return finalizeAuthorizationCodeLocked(authorizationCode);
  }

  auto finalizeAuthorizationCodeLocked(const LoopbackAuthorizationCode &authorizationCode) -> bool
  {
    if (!expectedState_.has_value())
    {
      pendingAuthorizationCode_ = authorizationCode;
      return false;
    }

    if (authorizationCode.state != *expectedState_)
    {
      completionSignaled_ = true;
      resultPromise_.set_exception(
        std::make_exception_ptr(LoopbackReceiverError(LoopbackReceiverErrorCode::kStateMismatch, "OAuth callback state mismatch; possible CSRF attempt detected")));
      return true;
    }

    completionSignaled_ = true;
    resultPromise_.set_value(authorizationCode);
    return false;
  }

  auto signalFailure(LoopbackReceiverErrorCode code, std::string message) -> void
  {
    std::scoped_lock lock(mutex_);
    if (completionSignaled_)
    {
      return;
    }

    completionSignaled_ = true;
    resultPromise_.set_exception(std::make_exception_ptr(LoopbackReceiverError(code, std::move(message))));
  }

private:
  LoopbackReceiverOptions options_;
  std::unique_ptr<transport::HttpServerRuntime> runtime_;

  mutable std::mutex mutex_;
  std::promise<LoopbackAuthorizationCode> resultPromise_;
  std::future<LoopbackAuthorizationCode> resultFuture_;
  std::optional<std::string> expectedState_;
  std::optional<LoopbackAuthorizationCode> pendingAuthorizationCode_;
  bool completionSignaled_ = false;
  bool resultConsumed_ = false;
};

LoopbackReceiverError::LoopbackReceiverError(LoopbackReceiverErrorCode code, const std::string &message)
  : std::runtime_error(message)
  , code_(code)
{
}

auto LoopbackReceiverError::code() const noexcept -> LoopbackReceiverErrorCode
{
  return code_;
}

LoopbackRedirectReceiver::LoopbackRedirectReceiver(LoopbackReceiverOptions options)
  : impl_(std::make_unique<Impl>(std::move(options)))
{
}

LoopbackRedirectReceiver::~LoopbackRedirectReceiver() = default;

LoopbackRedirectReceiver::LoopbackRedirectReceiver(LoopbackRedirectReceiver &&other) noexcept = default;

auto LoopbackRedirectReceiver::operator=(LoopbackRedirectReceiver &&other) noexcept -> LoopbackRedirectReceiver & = default;

auto LoopbackRedirectReceiver::start() -> void
{
  impl_->start();
}

auto LoopbackRedirectReceiver::stop() noexcept -> void
{
  impl_->stop();
}

auto LoopbackRedirectReceiver::isRunning() const noexcept -> bool
{
  return impl_->isRunning();
}

auto LoopbackRedirectReceiver::localPort() const noexcept -> std::uint16_t
{
  return impl_->localPort();
}

auto LoopbackRedirectReceiver::callbackPath() const noexcept -> const std::string &
{
  return impl_->callbackPath();
}

auto LoopbackRedirectReceiver::redirectUri() const -> std::string
{
  return impl_->redirectUri();
}

auto LoopbackRedirectReceiver::waitForAuthorizationCode(std::string expectedState, std::optional<std::chrono::milliseconds> timeoutOverride) -> LoopbackAuthorizationCode
{
  return impl_->waitForAuthorizationCode(std::move(expectedState), timeoutOverride);
}

}  // namespace mcp::auth

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
// hicpp-signed-bitwise, misc-const-correctness, misc-include-cleaner)
