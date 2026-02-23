#pragma once

// Umbrella header for the mcp/auth module
// This header includes all auth types. For faster compile times,
// prefer including specific headers directly.

// Auth provider types
#include <mcp/auth/auth_provider.hpp>
#include <mcp/auth/auth_request_context.hpp>
#include <mcp/auth/auth_result.hpp>
#include <mcp/auth/auth_verifier.hpp>

// OAuth server types
#include <mcp/auth/oauth_authorization_context.hpp>
#include <mcp/auth/oauth_authorization_request_context.hpp>
#include <mcp/auth/oauth_protected_resource_metadata.hpp>
#include <mcp/auth/oauth_protected_resource_metadata_publication.hpp>
#include <mcp/auth/oauth_required_scope_resolver.hpp>
#include <mcp/auth/oauth_scope_set.hpp>
#include <mcp/auth/oauth_server_authorization_options.hpp>
#include <mcp/auth/oauth_token_verification_request.hpp>
#include <mcp/auth/oauth_token_verification_result.hpp>
#include <mcp/auth/oauth_token_verification_status.hpp>
#include <mcp/auth/oauth_token_verifier.hpp>

// Client registration types
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

// Loopback receiver types
#include <mcp/auth/loopback_authorization_code.hpp>
#include <mcp/auth/loopback_receiver_error.hpp>
#include <mcp/auth/loopback_receiver_options.hpp>
#include <mcp/auth/loopback_redirect_receiver.hpp>

// Protected resource metadata and discovery types
#include <mcp/auth/authorization_discovery_error.hpp>
#include <mcp/auth/authorization_discovery_request.hpp>
#include <mcp/auth/authorization_discovery_result.hpp>
#include <mcp/auth/authorization_server_metadata.hpp>
#include <mcp/auth/bearer_www_authenticate_challenge.hpp>
#include <mcp/auth/bearer_www_authenticate_parameter.hpp>
#include <mcp/auth/discovery_header.hpp>
#include <mcp/auth/discovery_http_request.hpp>
#include <mcp/auth/discovery_http_response.hpp>
#include <mcp/auth/discovery_http_types.hpp>
#include <mcp/auth/discovery_security_policy.hpp>
#include <mcp/auth/protected_resource_metadata_data.hpp>

// OAuth client types
#include <mcp/auth/in_memory_oauth_token_storage.hpp>
#include <mcp/auth/oauth_access_token.hpp>
#include <mcp/auth/oauth_authorization_url_request.hpp>
#include <mcp/auth/oauth_client.hpp>
#include <mcp/auth/oauth_client_error.hpp>
#include <mcp/auth/oauth_client_error_code.hpp>
#include <mcp/auth/oauth_http_header.hpp>
#include <mcp/auth/oauth_http_request_executor.hpp>
#include <mcp/auth/oauth_http_response.hpp>
#include <mcp/auth/oauth_http_security_policy.hpp>
#include <mcp/auth/oauth_protected_resource_request.hpp>
#include <mcp/auth/oauth_protected_resource_request_executor.hpp>
#include <mcp/auth/oauth_query_parameter.hpp>
#include <mcp/auth/oauth_step_up_authorization_request.hpp>
#include <mcp/auth/oauth_step_up_authorizer.hpp>
#include <mcp/auth/oauth_step_up_execution_request.hpp>
#include <mcp/auth/oauth_token_exchange_request.hpp>
#include <mcp/auth/oauth_token_http_request.hpp>
#include <mcp/auth/oauth_token_request_execution_request.hpp>
#include <mcp/auth/oauth_token_storage.hpp>
#include <mcp/auth/pkce_code_pair.hpp>
