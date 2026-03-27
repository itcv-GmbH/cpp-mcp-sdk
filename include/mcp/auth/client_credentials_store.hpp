#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <mcp/auth/resolved_client_identity.hpp>
#include <mcp/export.hpp>

namespace mcp::auth
{

class MCP_SDK_EXPORT ClientCredentialsStore
{
public:
  ClientCredentialsStore() = default;
  ClientCredentialsStore(const ClientCredentialsStore &) = default;
  auto operator=(const ClientCredentialsStore &) -> ClientCredentialsStore & = default;
  ClientCredentialsStore(ClientCredentialsStore &&) noexcept = default;
  auto operator=(ClientCredentialsStore &&) noexcept -> ClientCredentialsStore & = default;
  virtual ~ClientCredentialsStore() = default;
  virtual auto load(std::string_view authorizationServerIssuer) const -> std::optional<ResolvedClientIdentity> = 0;
  virtual auto save(std::string authorizationServerIssuer, ResolvedClientIdentity identity) -> void = 0;
};

}  // namespace mcp::auth
