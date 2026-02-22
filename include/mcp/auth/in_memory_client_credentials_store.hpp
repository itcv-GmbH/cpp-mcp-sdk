#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <mcp/auth/client_credentials_store.hpp>

namespace mcp::auth
{

class InMemoryClientCredentialsStore final : public ClientCredentialsStore
{
public:
  auto load(std::string_view authorizationServerIssuer) const -> std::optional<ResolvedClientIdentity> override;
  auto save(std::string authorizationServerIssuer, ResolvedClientIdentity identity) -> void override;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ResolvedClientIdentity> credentialsByIssuer_;
};

}  // namespace mcp::auth
