#pragma once

#include <cstddef>
#include <string>
#include <string_view>

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
#include <mcp/auth/protected_resource_metadata.hpp>

namespace mcp::auth
{

auto validateAuthorizationServerPkceS256Support(const AuthorizationServerMetadata &metadata) -> void;
auto validateRedirectUriForAuthorizationCodeFlow(std::string_view redirectUri) -> void;
auto generatePkceCodePair(std::size_t verifierEntropyBytes = kDefaultPkceVerifierEntropyBytes) -> PkceCodePair;
auto buildAuthorizationUrl(const OAuthAuthorizationUrlRequest &request) -> std::string;
auto buildTokenExchangeHttpRequest(const OAuthTokenExchangeRequest &request) -> OAuthTokenHttpRequest;
auto executeTokenRequestWithPolicy(const OAuthTokenRequestExecutionRequest &request) -> OAuthHttpResponse;
auto executeProtectedResourceRequestWithStepUp(const OAuthStepUpExecutionRequest &request) -> OAuthHttpResponse;

}  // namespace mcp::auth
