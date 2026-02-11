#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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

}  // namespace mcp::transport
