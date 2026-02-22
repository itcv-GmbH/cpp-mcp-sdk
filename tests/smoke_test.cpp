// Smoke test for MCP SDK
#include <cstddef>
#include <cstdlib>
#include <string>

#include <mcp/auth/provider.hpp>
#include <mcp/client/client.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/security/origin_policy.hpp>
#include <mcp/server/server.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/stdio.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/sdk/version.hpp>

auto main() -> int
{
  const std::size_t apiSurfaceSanity = sizeof(mcp::auth::AuthProvider *) + sizeof(mcp::Client *) + sizeof(mcp::JsonRpcError) + sizeof(mcp::jsonrpc::Message)
    + sizeof(mcp::jsonrpc::Router) + sizeof(mcp::Session *) + sizeof(mcp::Server *) + sizeof(mcp::transport::StdioTransport *) + sizeof(mcp::transport::Transport *)
    + sizeof(mcp::security::OriginPolicy);

  if (apiSurfaceSanity == 0)
  {
    return EXIT_FAILURE;
  }

  const char *sdkVersion = mcp::getLibraryVersion();
  if (sdkVersion == nullptr)
  {
    return EXIT_FAILURE;
  }

  if (mcp::kLatestProtocolVersion.empty())
  {
    return EXIT_FAILURE;
  }

  mcp::NegotiatedProtocolVersion negotiatedVersion;
  negotiatedVersion.setNegotiatedProtocolVersion(std::string(mcp::kLatestProtocolVersion));
  if (!negotiatedVersion.hasNegotiatedProtocolVersion())
  {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
