#pragma once

#include <functional>
#include <memory>

#include <mcp/auth/client_credentials_store.hpp>
#include <mcp/auth/client_registration_http_request.hpp>
#include <mcp/auth/client_registration_http_response.hpp>
#include <mcp/auth/client_registration_strategy_configuration.hpp>
#include <mcp/auth/protected_resource_metadata.hpp>

namespace mcp::auth
{

using ClientRegistrationHttpExecutor = std::function<ClientRegistrationHttpResponse(const ClientRegistrationHttpRequest &)>;

struct ResolveClientRegistrationRequest
{
  AuthorizationServerMetadata authorizationServerMetadata;
  ClientRegistrationStrategyConfiguration strategyConfiguration;
  std::shared_ptr<ClientCredentialsStore> credentialsStore = std::make_shared<InMemoryClientCredentialsStore>();
  ClientRegistrationHttpExecutor httpExecutor;
};

}  // namespace mcp::auth
