#pragma once

/**
 * @file oauth_server.hpp
 * @brief Umbrella header for OAuth server types.
 *
 * This header provides backward compatibility by including all individual
 * OAuth server component headers. New code may include specific headers
 * directly for faster compile times.
 */

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

namespace mcp::auth
{

// This namespace re-exports all types from the individual headers
// for backward compatibility.

}  // namespace mcp::auth
