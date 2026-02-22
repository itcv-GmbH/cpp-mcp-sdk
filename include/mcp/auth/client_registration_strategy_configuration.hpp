#pragma once

#include <optional>

#include <mcp/auth/client_id_metadata_document_configuration.hpp>
#include <mcp/auth/dynamic_client_registration_configuration.hpp>
#include <mcp/auth/pre_registered_client_configuration.hpp>

namespace mcp::auth
{

struct ClientRegistrationStrategyConfiguration
{
  std::optional<PreRegisteredClientConfiguration> preRegistered;
  std::optional<ClientIdMetadataDocumentConfiguration> clientIdMetadataDocument;
  DynamicClientRegistrationConfiguration dynamic;
};

}  // namespace mcp::auth
