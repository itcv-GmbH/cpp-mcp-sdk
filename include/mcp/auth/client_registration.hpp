#pragma once

// Umbrella header for client registration types
// This header includes all per-type headers for backward compatibility.
// Prefer including individual headers directly.

#include <mcp/auth/client_authentication_method.hpp>
#include <mcp/auth/client_credentials_store.hpp>
#include <mcp/auth/client_id_metadata_document_configuration.hpp>
#include <mcp/auth/client_registration_error.hpp>
#include <mcp/auth/client_registration_header.hpp>
#include <mcp/auth/client_registration_http_request.hpp>
#include <mcp/auth/client_registration_http_response.hpp>
#include <mcp/auth/client_registration_result.hpp>
#include <mcp/auth/client_registration_strategy.hpp>
#include <mcp/auth/client_registration_strategy_configuration.hpp>
#include <mcp/auth/dynamic_client_registration_configuration.hpp>
#include <mcp/auth/in_memory_client_credentials_store.hpp>
#include <mcp/auth/pre_registered_client_configuration.hpp>
#include <mcp/auth/resolve_client_registration_request.hpp>
#include <mcp/auth/resolved_client_identity.hpp>

// Function declarations
namespace mcp::auth
{

auto validateClientIdMetadataDocumentClientIdUrl(std::string_view clientIdUrl) -> void;
auto buildClientIdMetadataDocumentPayload(const ClientIdMetadataDocumentConfiguration &configuration) -> std::string;
auto validateClientIdMetadataDocumentPayload(std::string_view metadataDocumentJson, std::string_view expectedClientIdUrl) -> void;
auto resolveClientRegistration(const ResolveClientRegistrationRequest &request) -> ClientRegistrationResult;

}  // namespace mcp::auth
