#pragma once

/**
 * @file all.hpp
 * @brief Umbrella header for lifecycle components.
 *
 * This header provides access to all lifecycle-related types.
 * For finer-grained control, include individual headers from mcp/lifecycle/session/.
 */

// Exception types
#include <mcp/lifecycle/session/capability_error.hpp>
#include <mcp/lifecycle/session/lifecycle_error.hpp>

// Threading and execution
#include <mcp/lifecycle/session/executor.hpp>
#include <mcp/lifecycle/session/handler_threading_policy.hpp>
#include <mcp/lifecycle/session/session_threading.hpp>

// Configuration
#include <mcp/lifecycle/session/request_options.hpp>
#include <mcp/lifecycle/session/response_callback.hpp>
#include <mcp/lifecycle/session/session_options.hpp>

// Session state and role
#include <mcp/lifecycle/session/session_role.hpp>
#include <mcp/lifecycle/session/session_state.hpp>

// UI and metadata
#include <mcp/lifecycle/session/icon.hpp>
#include <mcp/lifecycle/session/implementation.hpp>

// Capabilities
#include <mcp/lifecycle/session/client_capabilities.hpp>
#include <mcp/lifecycle/session/completions_capability.hpp>
#include <mcp/lifecycle/session/elicitation_capability.hpp>
#include <mcp/lifecycle/session/logging_capability.hpp>
#include <mcp/lifecycle/session/prompts_capability.hpp>
#include <mcp/lifecycle/session/resources_capability.hpp>
#include <mcp/lifecycle/session/roots_capability.hpp>
#include <mcp/lifecycle/session/sampling_capability.hpp>
#include <mcp/lifecycle/session/server_capabilities.hpp>
#include <mcp/lifecycle/session/tasks_capability.hpp>
#include <mcp/lifecycle/session/tools_capability.hpp>

// Negotiated parameters and main session class
#include <mcp/lifecycle/session.hpp>
#include <mcp/lifecycle/session/negotiated_parameters.hpp>
