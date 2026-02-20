#pragma once

// Runners provide high-level APIs for running MCP servers over various transports.
// This header provides access to all runner types for convenient inclusion.
//
// Runners live in namespace mcp to align with the existing public API surface where
// core types like Server, Client, and Session reside.
//
// For session isolation details when running multiple clients over HTTP,
// refer to task-002 which defines the session isolation contract.
//
// Usage example:
// @code
//   // Create a server factory that produces configured Server instances
//   mcp::ServerFactory makeServer = [] {
//     auto server = mcp::Server::create();
//     server->registerTool(...);
//     return server;
//   };
//
//   // Use with a specific runner
//   mcp::StdioServerRunner runner(makeServer);
//   runner.run();
// @endcode

#include <mcp/server/combined_runner.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/server/streamable_http_runner.hpp>
