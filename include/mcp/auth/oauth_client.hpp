#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <mcp/auth/authorization_discovery_result.hpp>
#include <mcp/auth/authorization_server_metadata.hpp>
#include <mcp/auth/in_memory_oauth_token_storage.hpp>
#include <mcp/auth/oauth_access_token.hpp>
#include <mcp/auth/oauth_authorization_url_request.hpp>
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
#include <mcp/export.hpp>

namespace mcp::auth
{

MCP_SDK_EXPORT auto validateAuthorizationServerPkceS256Support(const AuthorizationServerMetadata &metadata) -> void;
MCP_SDK_EXPORT auto validateRedirectUriForAuthorizationCodeFlow(std::string_view redirectUri) -> void;
MCP_SDK_EXPORT auto generatePkceCodePair(std::size_t verifierEntropyBytes = kDefaultPkceVerifierEntropyBytes) -> PkceCodePair;
MCP_SDK_EXPORT auto buildAuthorizationUrl(const OAuthAuthorizationUrlRequest &request) -> std::string;
MCP_SDK_EXPORT auto buildTokenExchangeHttpRequest(const OAuthTokenExchangeRequest &request) -> OAuthTokenHttpRequest;
MCP_SDK_EXPORT auto executeTokenRequestWithPolicy(const OAuthTokenRequestExecutionRequest &request) -> OAuthHttpResponse;
MCP_SDK_EXPORT auto executeProtectedResourceRequestWithStepUp(const OAuthStepUpExecutionRequest &request) -> OAuthHttpResponse;

}  // namespace mcp::auth
