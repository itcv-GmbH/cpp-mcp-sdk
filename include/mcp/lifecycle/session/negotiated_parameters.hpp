#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <mcp/lifecycle/session/client_capabilities.hpp>
#include <mcp/lifecycle/session/implementation.hpp>
#include <mcp/lifecycle/session/server_capabilities.hpp>



namespace mcp::lifecycle::session
{

/**
 * @brief Stores negotiated version and capabilities.
 */
class NegotiatedParameters
{
public:
  NegotiatedParameters() = default;
  NegotiatedParameters(std::string protocolVersion,
                       ClientCapabilities clientCaps,
                       ServerCapabilities serverCaps,
                       Implementation clientInfo,
                       Implementation serverInfo,
                       std::optional<std::string> instructions);

  auto protocolVersion() const noexcept -> std::string_view;
  auto clientCapabilities() const noexcept -> const ClientCapabilities &;
  auto serverCapabilities() const noexcept -> const ServerCapabilities &;
  auto clientInfo() const noexcept -> const Implementation &;
  auto serverInfo() const noexcept -> const Implementation &;
  auto instructions() const noexcept -> const std::optional<std::string> &;

private:
  std::string protocolVersion_;
  ClientCapabilities clientCapabilities_;
  ServerCapabilities serverCapabilities_;
  Implementation clientInfo_;
  Implementation serverInfo_;
  std::optional<std::string> instructions_;
};

} // namespace mcp::lifecycle::session


