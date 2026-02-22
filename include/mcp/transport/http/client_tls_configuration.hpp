#pragma once

#include <optional>
#include <string>

namespace mcp::transport::http
{

struct ClientTlsConfiguration
{
  bool verifyPeer = true;
  std::optional<std::string> caCertificateFile;
  std::optional<std::string> caCertificatePath;
  std::optional<std::string> serverNameIndication;
};

}  // namespace mcp::transport::http