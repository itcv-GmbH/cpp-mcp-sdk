#pragma once

// Umbrella header for MCP server module
// This header includes all server-related headers

// Core server
#include <mcp/server/server.hpp>
#include <mcp/server/server_configuration.hpp>

// Tools, Resources, Prompts
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/tools.hpp>

// Runners
#include <mcp/server/combined_runner.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/server/streamable_http_runner.hpp>

// Supporting types
#include <mcp/server/completion_types.hpp>
#include <mcp/server/list_endpoint.hpp>
#include <mcp/server/log_level.hpp>
#include <mcp/server/pagination_window.hpp>
