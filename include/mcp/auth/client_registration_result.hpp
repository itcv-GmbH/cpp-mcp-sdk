#pragma once

#include <mcp/auth/client_registration_strategy.hpp>
#include <mcp/auth/resolved_client_identity.hpp>

namespace mcp::auth
{

struct ClientRegistrationResult
{
  ClientRegistrationStrategy strategy = ClientRegistrationStrategy::kPreRegistered;
  ResolvedClientIdentity client;
};

}  // namespace mcp::auth
