#include <atomic>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include <mcp/detail/thread_boundary.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/sdk/error_reporter.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/stdio_runner.hpp>

namespace
{

constexpr std::string_view kName = "StdioServerRunner";  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

}  // namespace

namespace mcp
{

struct StdioServerRunner::Impl
{
  explicit Impl(ServerFactory serverFactoryIn, StdioServerRunnerOptions optionsIn)
    : serverFactory(std::move(serverFactoryIn))
    , options(std::move(optionsIn))
  {
  }

  ServerFactory serverFactory;
  StdioServerRunnerOptions options;
  std::shared_ptr<Server> server;
  std::atomic<bool> stopRequested {false};

  static auto writeMessage(const jsonrpc::Message &message, std::ostream &output) -> void
  {
    const auto serialized = jsonrpc::serializeMessage(message, jsonrpc::EncodeOptions {.disallowEmbeddedNewlines = true});
    output << serialized << '\n';
    output.flush();
  }

  auto writeError(const std::exception &error, std::ostream &errorStream) const -> void
  {
    if (options.transportOptions.allowStderrLogs)
    {
      errorStream << kName << " error: " << error.what() << '\n';
      errorStream.flush();
    }
  }

  auto writeError(std::string_view message, std::ostream &errorStream) const -> void
  {
    if (options.transportOptions.allowStderrLogs)
    {
      errorStream << kName << " error: " << message << '\n';
      errorStream.flush();
    }
  }

  auto processMessage(const jsonrpc::Message &message, const jsonrpc::RequestContext &context, std::ostream &output, std::ostream &errorStream) const -> void;
};

StdioServerRunner::StdioServerRunner(ServerFactory serverFactory)
  : StdioServerRunner(std::move(serverFactory), StdioServerRunnerOptions {})
{
}

StdioServerRunner::StdioServerRunner(ServerFactory serverFactory, StdioServerRunnerOptions options)
  : impl_(std::make_unique<Impl>(std::move(serverFactory), std::move(options)))
{
}

StdioServerRunner::~StdioServerRunner() = default;

StdioServerRunner::StdioServerRunner(StdioServerRunner &&other) noexcept = default;

auto StdioServerRunner::operator=(StdioServerRunner &&other) noexcept -> StdioServerRunner & = default;

auto StdioServerRunner::run() -> void
{
  run(std::cin, std::cout, std::cerr);
}

namespace
{

namespace detail
{

// No-op helper for suppresssing exceptions in destructors
inline auto suppressException() noexcept -> void {}  // NOLINT(llvm-prefer-static-over-anonymous-namespace)

}  // namespace detail

/// RAII guard to ensure server stop is called on all exit paths
class ServerStopGuard final
{
public:
  explicit ServerStopGuard(std::shared_ptr<Server> serverIn)
    : server_(std::move(serverIn))
  {
  }

  ~ServerStopGuard() noexcept
  {
    if (server_ != nullptr)
    {
      try
      {
        server_->stop();
      }
      catch (...)
      {
        // Suppress all exceptions from stop() - it must not throw from destructor
        detail::suppressException();
      }
    }
  }

  // Non-copyable, non-movable
  ServerStopGuard(const ServerStopGuard &) = delete;
  auto operator=(const ServerStopGuard &) -> ServerStopGuard & = delete;
  ServerStopGuard(ServerStopGuard &&) = delete;
  auto operator=(ServerStopGuard &&) -> ServerStopGuard & = delete;

private:
  std::shared_ptr<Server> server_;
};

}  // namespace

auto StdioServerRunner::run(std::istream &input, std::ostream &output, std::ostream &errorStream) -> void
{
  // Create exactly one Server instance via the factory
  impl_->server = impl_->serverFactory();

  // Set up the outbound message sender to write serialized JSON-RPC to output
  impl_->server->setOutboundMessageSender([&output](const jsonrpc::RequestContext &, const jsonrpc::Message &message) -> void { Impl::writeMessage(message, output); });

  // Call server->start() before processing messages
  impl_->server->start();

  // RAII guard ensures server->stop() is called on all exit paths (including exceptions)
  const ServerStopGuard stopGuard(impl_->server);

  const auto &limits = impl_->options.transportOptions.limits;
  const auto maxMessageSize = limits.maxMessageSizeBytes;

  const jsonrpc::RequestContext context {};

  std::string line;

  // Input loop: read lines until EOF or stop requested
  while (!impl_->stopRequested.load() && std::getline(input, line))
  {
    // Ignore empty lines
    if (line.empty())
    {
      continue;
    }

    // Check for unterminated EOF frame
    if (input.eof() && !line.empty())
    {
      // Reject unterminated EOF frame - write parse error to output
      Impl::writeMessage(jsonrpc::Message {jsonrpc::makeUnknownIdErrorResponse(jsonrpc::makeParseError(std::nullopt, "Unterminated EOF frame: message not followed by newline"))},
                         output);
      break;
    }

    // Reject lines that exceed maxMessageSizeBytes before parsing (unconditionally enforced)
    if (line.size() > static_cast<std::size_t>(maxMessageSize))
    {
      Impl::writeMessage(jsonrpc::Message {jsonrpc::makeUnknownIdErrorResponse(
                           jsonrpc::makeParseError(std::nullopt, "Message exceeds maximum size limit of " + std::to_string(maxMessageSize) + " bytes"))},
                         output);
      continue;
    }

    try
    {
      const jsonrpc::Message message = jsonrpc::parseMessage(line);

      impl_->processMessage(message, context, output, errorStream);
    }
    catch (const std::exception &error)
    {
      impl_->writeError(error, errorStream);
      // Report error through error reporter if configured
      reportError(impl_->options.errorReporter, "StdioServerRunner", error.what());
      // Emit parse error response with fixed non-sensitive message
      Impl::writeMessage(jsonrpc::Message {jsonrpc::makeUnknownIdErrorResponse(jsonrpc::makeParseError(std::nullopt, "Parse error"))}, output);
    }
    catch (...)
    {
      impl_->writeError("Unknown exception", errorStream);
      // Report unknown exception through error reporter if configured
      reportError(impl_->options.errorReporter, "StdioServerRunner", "Unknown exception");
      // Emit parse error response with fixed non-sensitive message
      Impl::writeMessage(jsonrpc::Message {jsonrpc::makeUnknownIdErrorResponse(jsonrpc::makeParseError(std::nullopt, "Parse error"))}, output);
    }
  }

  // Guard destructor will call server->stop() on exit
}

auto StdioServerRunner::Impl::processMessage(const jsonrpc::Message &message, const jsonrpc::RequestContext &context, std::ostream &output, std::ostream &errorStream) const -> void
{
  if (std::holds_alternative<jsonrpc::Request>(message))
  {
    const auto &request = std::get<jsonrpc::Request>(message);
    try
    {
      const jsonrpc::Response response = server->handleRequest(context, request).get();
      // Write response - convert Response variant to Message variant
      if (std::holds_alternative<jsonrpc::SuccessResponse>(response))
      {
        Impl::writeMessage(jsonrpc::Message {std::get<jsonrpc::SuccessResponse>(response)}, output);
      }
      else
      {
        Impl::writeMessage(jsonrpc::Message {std::get<jsonrpc::ErrorResponse>(response)}, output);
      }
    }
    catch (const std::exception &error)
    {
      writeError(error, errorStream);
      // Report error through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", error.what());
      // Emit internal error response with fixed non-sensitive message
      Impl::writeMessage(jsonrpc::Message {jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Internal error"), request.id)}, output);
    }
    catch (...)
    {
      writeError("Unknown exception", errorStream);
      // Report unknown exception through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", "Unknown exception");
      // Emit internal error response with fixed non-sensitive message
      Impl::writeMessage(jsonrpc::Message {jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Internal error"), request.id)}, output);
    }
  }
  else if (std::holds_alternative<jsonrpc::Notification>(message))
  {
    const auto &notification = std::get<jsonrpc::Notification>(message);
    try
    {
      server->handleNotification(context, notification);
    }
    catch (const std::exception &error)
    {
      writeError(error, errorStream);
      // Report error through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", error.what());
      // For notification dispatch exceptions, emit no output
    }
    catch (...)
    {
      writeError("Unknown exception", errorStream);
      // Report unknown exception through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", "Unknown exception");
      // For notification dispatch exceptions, emit no output
    }
  }
  else if (std::holds_alternative<jsonrpc::SuccessResponse>(message))
  {
    const auto response = jsonrpc::Response {std::get<jsonrpc::SuccessResponse>(message)};
    try
    {
      static_cast<void>(server->handleResponse(context, response));
    }
    catch (const std::exception &error)
    {
      writeError(error, errorStream);
      // Report error through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", error.what());
      // For response dispatch exceptions, emit no output
    }
    catch (...)
    {
      writeError("Unknown exception", errorStream);
      // Report unknown exception through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", "Unknown exception");
      // For response dispatch exceptions, emit no output
    }
  }
  else if (std::holds_alternative<jsonrpc::ErrorResponse>(message))
  {
    const auto response = jsonrpc::Response {std::get<jsonrpc::ErrorResponse>(message)};
    try
    {
      static_cast<void>(server->handleResponse(context, response));
    }
    catch (const std::exception &error)
    {
      writeError(error, errorStream);
      // Report error through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", error.what());
      // For response dispatch exceptions, emit no output
    }
    catch (...)
    {
      writeError("Unknown exception", errorStream);
      // Report unknown exception through error reporter if configured
      reportError(options.errorReporter, "StdioServerRunner", "Unknown exception");
      // For response dispatch exceptions, emit no output
    }
  }
}

auto StdioServerRunner::startAsync() -> std::thread
{
  return std::thread(mcp::detail::threadBoundary([this]() -> void { run(); }, impl_->options.errorReporter, "StdioServerRunner"));
}

auto StdioServerRunner::stop() noexcept -> void
{
  impl_->stopRequested.store(true);
}

auto StdioServerRunner::options() const -> const StdioServerRunnerOptions &
{
  return impl_->options;
}

}  // namespace mcp
