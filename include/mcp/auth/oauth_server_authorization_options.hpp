#pragma once

#include <memory>

#include <mcp/auth/oauth_protected_resource_metadata.hpp>
#include <mcp/auth/oauth_protected_resource_metadata_publication.hpp>
#include <mcp/auth/oauth_required_scope_resolver.hpp>
#include <mcp/auth/oauth_scope_set.hpp>
#include <mcp/auth/oauth_token_verifier.hpp>

namespace mcp::auth
{

struct OAuthServerAuthorizationOptions
{
  std::shared_ptr<const OAuthTokenVerifier> tokenVerifier;
  OAuthProtectedResourceMetadata protectedResourceMetadata;
  OAuthProtectedResourceMetadataPublication metadataPublication;
  OAuthRequiredScopeResolver requiredScopesResolver;
  OAuthScopeSet defaultRequiredScopes;
};

}  // namespace mcp::auth
