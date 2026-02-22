#pragma once

/**
 * @brief MCP Client umbrella header.
 *
 * This header includes all client-related headers for convenience.
 * For finer-grained includes, use individual headers:
 * - mcp/client/client_class.hpp - Main Client class
 * - mcp/client/types.hpp - Types and constants (ClientInitializeConfiguration, kDefaultMaxPaginationPages)
 * - mcp/client/list_tools_result.hpp - ListToolsResult
 * - mcp/client/list_resources_result.hpp - ListResourcesResult
 * - mcp/client/read_resource_result.hpp - ReadResourceResult
 * - mcp/client/list_resource_templates_result.hpp - ListResourceTemplatesResult
 * - mcp/client/list_prompts_result.hpp - ListPromptsResult
 * - mcp/client/roots.hpp - RootsProvider, RootEntry, RootsListContext
 * - mcp/client/sampling.hpp - SamplingCreateMessageHandler, SamplingCreateMessageContext
 * - mcp/client/elicitation.hpp - Elicitation handlers and types
 */

#include <mcp/client/client_class.hpp>
#include <mcp/client/elicitation.hpp>
#include <mcp/client/list_prompts_result.hpp>
#include <mcp/client/list_resource_templates_result.hpp>
#include <mcp/client/list_resources_result.hpp>
#include <mcp/client/list_tools_result.hpp>
#include <mcp/client/read_resource_result.hpp>
#include <mcp/client/roots.hpp>
#include <mcp/client/sampling.hpp>
#include <mcp/client/types.hpp>
