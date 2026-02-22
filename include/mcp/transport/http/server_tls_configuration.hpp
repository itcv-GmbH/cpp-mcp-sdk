#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mcp::transport::http
{

enum class TlsClientAuthenticationMode : std::uint8_t
{
  kNone,
  kOptional,
  kRequired,
};

struct ServerTlsConfiguration
{
  std::string certificateChainFile;
  std::string privateKeyFile;
  std::optional<std::string> privateKeyPassphrase;
  TlsClientAuthenticationMode clientAuthenticationMode = TlsClientAuthenticationMode::kNone;
  std::optional<std::string> clientCaCertificateFile;
  std::optional<std::string> clientCaCertificatePath;
};

}  // namespace mcp::transport::http