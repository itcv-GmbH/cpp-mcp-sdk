#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <mcp/auth/oauth_access_token.hpp>
#include <mcp/auth/oauth_token_storage.hpp>
#include <mcp/export.hpp>

namespace mcp::auth
{

class MCP_SDK_EXPORT InMemoryOAuthTokenStorage final : public OAuthTokenStorage
{
public:
  auto load(std::string_view resource) const -> std::optional<OAuthAccessToken> override;
  auto save(std::string resource, OAuthAccessToken token) -> void override;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, OAuthAccessToken> tokensByResource_;
};

}  // namespace mcp::auth
