#include <algorithm>
#include <cstdint>
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

#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/transport/stdio.hpp>

namespace mcp::transport
{
namespace detail
{

static auto containsFramingNewline(std::string_view text) -> bool
{
  return std::any_of(text.begin(), text.end(), [](char character) -> bool { return character == '\n' || character == '\r'; });
}

static auto isValidUtf8(std::string_view text) -> bool
{
  std::size_t index = 0;
  while (index < text.size())
  {
    const unsigned char leadByte = static_cast<unsigned char>(text[index]);

    if (leadByte <= 0x7F)
    {
      ++index;
      continue;
    }

    std::size_t continuationCount = 0;
    std::uint32_t codePoint = 0;

    if ((leadByte & 0xE0U) == 0xC0U)
    {
      continuationCount = 1;
      codePoint = static_cast<std::uint32_t>(leadByte & 0x1FU);
      if (codePoint == 0)
      {
        return false;
      }
    }
    else if ((leadByte & 0xF0U) == 0xE0U)
    {
      continuationCount = 2;
      codePoint = static_cast<std::uint32_t>(leadByte & 0x0FU);
    }
    else if ((leadByte & 0xF8U) == 0xF0U)
    {
      continuationCount = 3;
      codePoint = static_cast<std::uint32_t>(leadByte & 0x07U);
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
      const unsigned char continuationByte = static_cast<unsigned char>(text[index + continuationIndex + 1]);
      if ((continuationByte & 0xC0U) != 0x80U)
      {
        return false;
      }

      codePoint = (codePoint << 6U) | static_cast<std::uint32_t>(continuationByte & 0x3FU);
    }

    if ((continuationCount == 1 && codePoint < 0x80U) || (continuationCount == 2 && codePoint < 0x800U) || (continuationCount == 3 && codePoint < 0x10000U) || codePoint > 0x10FFFFU
        || (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
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

static auto writeFramedMessage(std::ostream &output, jsonrpc::Message message) -> void
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

}  // namespace detail

StdioTransport::StdioTransport(StdioServerOptions options)
  : serverOptions_(options)
  , mode_(Mode::kServer)
{
}

StdioTransport::StdioTransport(StdioClientOptions options)
  : clientOptions_(std::move(options))
  , mode_(Mode::kClient)
{
}

auto StdioTransport::attach(std::weak_ptr<Session> session) -> void
{
  session_ = std::move(session);
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

  detail::writeFramedMessage(std::cout, std::move(message));
}

auto StdioTransport::run(jsonrpc::Router &router, StdioServerOptions options) -> void
{
  StdioAttachOptions attachOptions;
  attachOptions.allowStderrLogs = options.allowStderrLogs;
  attachOptions.emitParseErrors = true;

  router.setOutboundMessageSender([](const jsonrpc::RequestContext &, jsonrpc::Message outboundMessage) -> void
                                  { detail::writeFramedMessage(std::cout, std::move(outboundMessage)); });

  std::string line;
  while (std::getline(std::cin, line))
  {
    static_cast<void>(routeIncomingLine(router, line, std::cout, options.allowStderrLogs ? &std::cerr : nullptr, attachOptions));
  }
}

auto StdioTransport::attach(jsonrpc::Router &router, std::istream &serverStdout, std::ostream &serverStdin, StdioAttachOptions options) -> void
{
  router.setOutboundMessageSender([&serverStdin](const jsonrpc::RequestContext &, jsonrpc::Message outboundMessage) -> void
                                  { detail::writeFramedMessage(serverStdin, std::move(outboundMessage)); });

  std::string line;
  while (std::getline(serverStdout, line))
  {
    static_cast<void>(routeIncomingLine(router, line, serverStdin, options.allowStderrLogs ? &std::cerr : nullptr, options));
  }
}

auto StdioTransport::routeIncomingLine(jsonrpc::Router &router, std::string_view line, std::ostream &output, std::ostream *stderrOutput, StdioAttachOptions options) -> bool
{
  return detail::routeLine(router, line, output, stderrOutput, options);
}

auto StdioTransport::stderrStream() -> std::ostream *
{
  if (!serverOptions_.allowStderrLogs)
  {
    return nullptr;
  }

  return &std::cerr;
}

}  // namespace mcp::transport
