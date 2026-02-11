#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <istream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

#include <boost/process/v1/args.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/env.hpp>
#include <boost/process/v1/environment.hpp>
#include <boost/process/v1/io.hpp>
#include <boost/process/v1/start_dir.hpp>

#if !defined(_WIN32)
#  include <signal.h>
#endif

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/transport/stdio.hpp>

namespace mcp::transport
{
namespace detail
{

static constexpr std::uint8_t kAsciiMask = 0x80U;
static constexpr std::uint8_t kUtf8ByteMask2 = 0xE0U;
static constexpr std::uint8_t kUtf8ByteMask3 = 0xF0U;
static constexpr std::uint8_t kUtf8ByteMask4 = 0xF8U;
static constexpr std::uint8_t kUtf8Lead2 = 0xC0U;
static constexpr std::uint8_t kUtf8Lead3 = 0xE0U;
static constexpr std::uint8_t kUtf8Lead4 = 0xF0U;
static constexpr std::uint8_t kUtf8ContinuationMask = 0xC0U;
static constexpr std::uint8_t kUtf8ContinuationTag = 0x80U;
static constexpr std::uint8_t kUtf8LeadPayload2 = 0x1FU;
static constexpr std::uint8_t kUtf8LeadPayload3 = 0x0FU;
static constexpr std::uint8_t kUtf8LeadPayload4 = 0x07U;
static constexpr std::uint8_t kUtf8ContinuationPayload = 0x3FU;
static constexpr std::uint32_t kUtf8Min2 = 0x80U;
static constexpr std::uint32_t kUtf8Min3 = 0x800U;
static constexpr std::uint32_t kUtf8Min4 = 0x10000U;
static constexpr std::uint32_t kUnicodeMax = 0x10FFFFU;
static constexpr std::uint32_t kHighSurrogateMin = 0xD800U;
static constexpr std::uint32_t kHighSurrogateMax = 0xDFFFU;
static constexpr std::uint32_t kUtf8ContinuationShift = 6U;

static auto containsFramingNewline(std::string_view text) -> bool
{
  return std::any_of(text.begin(), text.end(), [](char character) -> bool { return character == '\n' || character == '\r'; });
}

static auto isValidUtf8(std::string_view text) -> bool
{
  std::size_t index = 0;
  while (index < text.size())
  {
    const auto leadByte = static_cast<unsigned char>(text[index]);

    if ((leadByte & kAsciiMask) == 0)
    {
      ++index;
      continue;
    }

    std::size_t continuationCount = 0;
    std::uint32_t codePoint = 0;

    if ((leadByte & kUtf8ByteMask2) == kUtf8Lead2)
    {
      continuationCount = 1;
      codePoint = static_cast<std::uint32_t>(leadByte & kUtf8LeadPayload2);
      if (codePoint == 0)
      {
        return false;
      }
    }
    else if ((leadByte & kUtf8ByteMask3) == kUtf8Lead3)
    {
      continuationCount = 2;
      codePoint = static_cast<std::uint32_t>(leadByte & kUtf8LeadPayload3);
    }
    else if ((leadByte & kUtf8ByteMask4) == kUtf8Lead4)
    {
      continuationCount = 3;
      codePoint = static_cast<std::uint32_t>(leadByte & kUtf8LeadPayload4);
    }
    else
    {
      return false;
    }

    if (index + continuationCount >= text.size())
    {
      return false;
    }

    for (std::size_t continuationIndex = 0; continuationIndex < continuationCount; ++continuationIndex)
    {
      const auto continuationByte = static_cast<unsigned char>(text[index + continuationIndex + 1]);
      if ((continuationByte & kUtf8ContinuationMask) != kUtf8ContinuationTag)
      {
        return false;
      }

      codePoint = (codePoint << kUtf8ContinuationShift) | static_cast<std::uint32_t>(continuationByte & kUtf8ContinuationPayload);
    }

    if ((continuationCount == 1 && codePoint < kUtf8Min2) || (continuationCount == 2 && codePoint < kUtf8Min3) || (continuationCount == 3 && codePoint < kUtf8Min4)
        || codePoint > kUnicodeMax || (codePoint >= kHighSurrogateMin && codePoint <= kHighSurrogateMax))
    {
      return false;
    }

    index += continuationCount + 1;
  }

  return true;
}

static auto logDiagnostic(std::ostream *stderrOutput, bool allowStderrLogs, std::string_view message) -> void
{
  if (!allowStderrLogs || stderrOutput == nullptr)
  {
    return;
  }

  *stderrOutput << message << '\n';
  stderrOutput->flush();
}

static auto writeFramedMessage(std::ostream &output, const jsonrpc::Message &message) -> void
{
  jsonrpc::EncodeOptions encodeOptions;
  encodeOptions.disallowEmbeddedNewlines = true;

  const std::string serializedMessage = jsonrpc::serializeMessage(message, encodeOptions);
  if (containsFramingNewline(serializedMessage))
  {
    throw jsonrpc::MessageValidationError("Serialized JSON-RPC message contains embedded newlines.");
  }

  if (!isValidUtf8(serializedMessage))
  {
    throw jsonrpc::MessageValidationError("Serialized JSON-RPC message is not valid UTF-8.");
  }

  output << serializedMessage << '\n';
  output.flush();
}

static auto dispatchInboundMessage(jsonrpc::Router &router, const jsonrpc::Message &message, std::ostream &output, std::ostream *stderrOutput, const StdioAttachOptions &options)
  -> void
{
  const jsonrpc::RequestContext context;

  if (std::holds_alternative<jsonrpc::Request>(message))
  {
    const jsonrpc::Request &request = std::get<jsonrpc::Request>(message);

    jsonrpc::Response response;
    try
    {
      response = router.dispatchRequest(context, request).get();
    }
    catch (const std::exception &error)
    {
      logDiagnostic(stderrOutput, options.allowStderrLogs, std::string("stdio transport request dispatch failed: ") + error.what());
      response = jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Request dispatch failed."), request.id);
    }

    std::visit([&output](const auto &typedResponse) -> void { writeFramedMessage(output, jsonrpc::Message {typedResponse}); }, response);
    return;
  }

  if (std::holds_alternative<jsonrpc::Notification>(message))
  {
    const jsonrpc::Notification &notification = std::get<jsonrpc::Notification>(message);
    router.dispatchNotification(context, notification);
    return;
  }

  const jsonrpc::Response response = std::holds_alternative<jsonrpc::SuccessResponse>(message) ? jsonrpc::Response {std::get<jsonrpc::SuccessResponse>(message)}
                                                                                               : jsonrpc::Response {std::get<jsonrpc::ErrorResponse>(message)};

  static_cast<void>(router.dispatchResponse(context, response));
}

static auto routeLine(jsonrpc::Router &router, std::string_view line, std::ostream &output, std::ostream *stderrOutput, const StdioAttachOptions &options) -> bool
{
  std::string normalizedLine(line);
  if (!normalizedLine.empty() && normalizedLine.back() == '\r')
  {
    normalizedLine.pop_back();
  }

  if (containsFramingNewline(normalizedLine))
  {
    logDiagnostic(stderrOutput, options.allowStderrLogs, "stdio transport rejected line containing framing newlines.");
    return false;
  }

  if (!isValidUtf8(normalizedLine))
  {
    logDiagnostic(stderrOutput, options.allowStderrLogs, "stdio transport rejected non UTF-8 input.");
    return false;
  }

  try
  {
    const jsonrpc::Message message = jsonrpc::parseMessage(normalizedLine);
    dispatchInboundMessage(router, message, output, stderrOutput, options);
    return true;
  }
  catch (const std::exception &error)
  {
    logDiagnostic(stderrOutput, options.allowStderrLogs, std::string("stdio transport parse failed: ") + error.what());

    if (options.emitParseErrors)
    {
      try
      {
        const jsonrpc::ErrorResponse parseError = jsonrpc::makeUnknownIdErrorResponse(jsonrpc::makeParseError());
        writeFramedMessage(output, jsonrpc::Message {parseError});
      }
      catch (const std::exception &writeError)
      {
        logDiagnostic(stderrOutput, options.allowStderrLogs, std::string("stdio transport failed writing parse error: ") + writeError.what());
      }
    }

    return false;
  }
}

static auto routeFramedInput(jsonrpc::Router &router, std::istream &input, std::ostream &output, std::ostream *stderrOutput, const StdioAttachOptions &options) -> void
{
  std::string line;
  while (std::getline(input, line))
  {
    if (input.eof())
    {
      logDiagnostic(stderrOutput, options.allowStderrLogs, "stdio transport rejected unterminated EOF frame.");
      return;
    }

    static_cast<void>(routeLine(router, line, output, stderrOutput, options));
  }
}

}  // namespace detail

namespace
{

namespace bp = boost::process::v1;

static auto sanitizeTimeout(std::chrono::milliseconds timeout) -> std::chrono::milliseconds
{
  if (timeout < std::chrono::milliseconds::zero())
  {
    return std::chrono::milliseconds::zero();
  }

  return timeout;
}

static auto buildSubprocessEnvironment(const std::vector<std::string> &envOverrides) -> bp::environment
{
  bp::environment environment = boost::this_process::environment();
  for (const std::string &entry : envOverrides)
  {
    const std::size_t separatorIndex = entry.find('=');
    if (separatorIndex == std::string::npos || separatorIndex == 0)
    {
      throw std::invalid_argument("Environment overrides must use KEY=VALUE format.");
    }

    const std::string key = entry.substr(0, separatorIndex);
    const std::string value = entry.substr(separatorIndex + 1);
    environment[key] = value;
  }

  return environment;
}

struct SpawnCommand
{
  std::string executable;
  std::vector<std::string> arguments;
};

static auto buildSpawnCommand(const StdioSubprocessSpawnOptions &options) -> SpawnCommand
{
  if (options.argv.empty())
  {
    throw std::invalid_argument("Subprocess argv must include at least the executable path.");
  }

  SpawnCommand command;
  command.executable = options.argv.front();
  if (options.argv.size() > 1)
  {
    command.arguments.assign(options.argv.begin() + 1, options.argv.end());
  }

  return command;
}

#if !defined(_WIN32)
static auto signalProcess(bp::child &process, int signalNumber) -> void
{
  if (!process.valid())
  {
    return;
  }

  if (::kill(process.id(), signalNumber) != 0 && errno != ESRCH)
  {
    throw std::system_error(errno, std::system_category(), "Failed to signal child process.");
  }
}
#endif

}  // namespace

struct StdioSubprocess::Impl
{
  bp::child process;
  bp::opstream stdinPipe;
  bp::ipstream stdoutPipe;
  bp::ipstream stderrPipe;
  StdioClientStderrMode stderrMode = StdioClientStderrMode::kCapture;
  bool stdinClosed = false;
  std::optional<int> exitCode;
  std::thread stderrReader;
  mutable std::mutex stderrMutex;
  std::string capturedStderr;

  auto startStderrCapture() -> void
  {
    if (stderrMode != StdioClientStderrMode::kCapture)
    {
      return;
    }

    stderrReader = std::thread(
      [this]() -> void
      {
        std::string line;
        while (std::getline(stderrPipe, line))
        {
          std::lock_guard<std::mutex> lock(stderrMutex);
          capturedStderr.append(line);
          capturedStderr.push_back('\n');
        }
      });
  }

  auto joinStderrCapture() noexcept -> void
  {
    if (stderrReader.joinable())
    {
      stderrReader.join();
    }
  }

  auto closeStdinPipe() noexcept -> void
  {
    if (stdinClosed)
    {
      return;
    }

    try
    {
      stdinPipe.flush();
      stdinPipe.pipe().close();
    }
    catch (...)
    {
    }

    stdinClosed = true;
  }

  auto markExited() -> void
  {
    exitCode = process.exit_code();
    joinStderrCapture();
  }
};

StdioSubprocess::StdioSubprocess() = default;

StdioSubprocess::StdioSubprocess(std::unique_ptr<Impl> impl)
  : impl_(std::move(impl))
{
}

StdioSubprocess::~StdioSubprocess()
{
  static_cast<void>(shutdown());
}

StdioSubprocess::StdioSubprocess(StdioSubprocess &&other) noexcept = default;

auto StdioSubprocess::operator=(StdioSubprocess &&other) noexcept -> StdioSubprocess & = default;

auto StdioSubprocess::valid() const noexcept -> bool
{
  return impl_ != nullptr && impl_->process.valid();
}

auto StdioSubprocess::writeLine(std::string_view line) -> void
{
  if (!valid())
  {
    throw std::runtime_error("Cannot write to an invalid subprocess.");
  }

  if (impl_->stdinClosed)
  {
    throw std::runtime_error("Cannot write to subprocess after stdin was closed.");
  }

  if (detail::containsFramingNewline(line))
  {
    throw std::invalid_argument("Subprocess writeLine expects a single line without framing newlines.");
  }

  impl_->stdinPipe << line << '\n';
  impl_->stdinPipe.flush();
  if (!impl_->stdinPipe.good())
  {
    throw std::runtime_error("Failed writing to subprocess stdin.");
  }
}

auto StdioSubprocess::readLine(std::string &line) -> bool
{
  if (!valid())
  {
    throw std::runtime_error("Cannot read from an invalid subprocess.");
  }

  return static_cast<bool>(std::getline(impl_->stdoutPipe, line));
}

auto StdioSubprocess::closeStdin() noexcept -> void
{
  if (impl_ == nullptr)
  {
    return;
  }

  impl_->closeStdinPipe();
}

auto StdioSubprocess::waitForExit(std::chrono::milliseconds timeout) -> bool
{
  if (!valid())
  {
    return true;
  }

  const auto boundedTimeout = sanitizeTimeout(timeout);
  if (!impl_->process.wait_for(boundedTimeout))
  {
    return false;
  }

  impl_->markExited();
  return true;
}

auto StdioSubprocess::shutdown(StdioSubprocessShutdownOptions options) noexcept -> bool
{
  if (impl_ == nullptr)
  {
    return true;
  }

  try
  {
    impl_->closeStdinPipe();

    if (waitForExit(options.waitForExitTimeout))
    {
      return true;
    }

#if defined(_WIN32)
    std::error_code terminateError;
    impl_->process.terminate(terminateError);
    static_cast<void>(terminateError);

    if (waitForExit(options.waitAfterTerminateTimeout))
    {
      return true;
    }

    impl_->process.terminate(terminateError);
    static_cast<void>(terminateError);
#else
    signalProcess(impl_->process, SIGTERM);
    if (waitForExit(options.waitAfterTerminateTimeout))
    {
      return true;
    }

    signalProcess(impl_->process, SIGKILL);
#endif

    if (!waitForExit(std::chrono::milliseconds {500}))
    {
      std::error_code waitError;
      impl_->process.wait(waitError);
      if (waitError)
      {
        return false;
      }

      impl_->markExited();
    }

    return true;
  }
  catch (...)
  {
    return false;
  }
}

auto StdioSubprocess::isRunning() -> bool
{
  if (!valid())
  {
    return false;
  }

  std::error_code runningError;
  const bool running = impl_->process.running(runningError);
  if (runningError)
  {
    throw std::system_error(runningError, "Failed to query subprocess state.");
  }

  if (!running)
  {
    impl_->markExited();
  }

  return running;
}

auto StdioSubprocess::exitCode() const -> std::optional<int>
{
  if (impl_ == nullptr)
  {
    return std::nullopt;
  }

  return impl_->exitCode;
}

auto StdioSubprocess::capturedStderr() const -> std::string
{
  if (impl_ == nullptr)
  {
    return {};
  }

  std::lock_guard<std::mutex> lock(impl_->stderrMutex);
  return impl_->capturedStderr;
}

StdioTransport::StdioTransport(StdioServerOptions options)
{
  static_cast<void>(options);
}

StdioTransport::StdioTransport(StdioClientOptions options)
{
  static_cast<void>(options);
}

auto StdioTransport::attach(std::weak_ptr<Session> session) -> void
{
  static_cast<void>(session);
}

auto StdioTransport::start() -> void
{
  running_ = true;
}

auto StdioTransport::stop() -> void
{
  running_ = false;
}

auto StdioTransport::isRunning() const noexcept -> bool
{
  return running_;
}

auto StdioTransport::send(jsonrpc::Message message) -> void
{
  if (!running_)
  {
    throw std::runtime_error("StdioTransport must be running before send().");
  }

  detail::writeFramedMessage(std::cout, message);
}

auto StdioTransport::run(jsonrpc::Router &router, StdioServerOptions options) -> void
{
  StdioAttachOptions attachOptions;
  attachOptions.allowStderrLogs = options.allowStderrLogs;
  attachOptions.emitParseErrors = true;

  router.setOutboundMessageSender([](const jsonrpc::RequestContext &, jsonrpc::Message outboundMessage) -> void
                                  { detail::writeFramedMessage(std::cout, std::move(outboundMessage)); });

  detail::routeFramedInput(router, std::cin, std::cout, options.allowStderrLogs ? &std::cerr : nullptr, attachOptions);
}

auto StdioTransport::attach(jsonrpc::Router &router, std::istream &serverStdout, std::ostream &serverStdin, StdioAttachOptions options) -> void
{
  router.setOutboundMessageSender([&serverStdin](const jsonrpc::RequestContext &, jsonrpc::Message outboundMessage) -> void
                                  { detail::writeFramedMessage(serverStdin, outboundMessage); });

  detail::routeFramedInput(router, serverStdout, serverStdin, options.allowStderrLogs ? &std::cerr : nullptr, options);
}

auto StdioTransport::routeIncomingLine(jsonrpc::Router &router, std::string_view line, std::ostream &output, std::ostream *stderrOutput, StdioAttachOptions options) -> bool
{
  return detail::routeLine(router, line, output, stderrOutput, options);
}

auto StdioTransport::spawnSubprocess(StdioSubprocessSpawnOptions options) -> StdioSubprocess
{
  const SpawnCommand command = buildSpawnCommand(options);
  const bp::environment environment = buildSubprocessEnvironment(options.envOverrides);

  auto impl = std::make_unique<StdioSubprocess::Impl>();
  impl->stderrMode = options.stderrMode;

  const bool useStartDir = !options.cwd.empty();
  switch (options.stderrMode)
  {
    case StdioClientStderrMode::kCapture:
      if (useStartDir)
      {
        impl->process = bp::child(command.executable,
                                  bp::args(command.arguments),
                                  bp::env(environment),
                                  bp::start_dir(options.cwd),
                                  bp::std_in = impl->stdinPipe,
                                  bp::std_out = impl->stdoutPipe,
                                  bp::std_err = impl->stderrPipe);
      }
      else
      {
        impl->process = bp::child(
          command.executable, bp::args(command.arguments), bp::env(environment), bp::std_in = impl->stdinPipe, bp::std_out = impl->stdoutPipe, bp::std_err = impl->stderrPipe);
      }
      impl->startStderrCapture();
      break;
    case StdioClientStderrMode::kForward:
      if (useStartDir)
      {
        impl->process = bp::child(
          command.executable, bp::args(command.arguments), bp::env(environment), bp::start_dir(options.cwd), bp::std_in = impl->stdinPipe, bp::std_out = impl->stdoutPipe);
      }
      else
      {
        impl->process = bp::child(command.executable, bp::args(command.arguments), bp::env(environment), bp::std_in = impl->stdinPipe, bp::std_out = impl->stdoutPipe);
      }
      break;
    case StdioClientStderrMode::kIgnore:
      if (useStartDir)
      {
        impl->process = bp::child(command.executable,
                                  bp::args(command.arguments),
                                  bp::env(environment),
                                  bp::start_dir(options.cwd),
                                  bp::std_in = impl->stdinPipe,
                                  bp::std_out = impl->stdoutPipe,
                                  bp::std_err = bp::null);
      }
      else
      {
        impl->process =
          bp::child(command.executable, bp::args(command.arguments), bp::env(environment), bp::std_in = impl->stdinPipe, bp::std_out = impl->stdoutPipe, bp::std_err = bp::null);
      }
      break;
  }

  return StdioSubprocess(std::move(impl));
}

}  // namespace mcp::transport
