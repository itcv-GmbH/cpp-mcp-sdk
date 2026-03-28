#pragma once

#include <optional>
#include <string>
#include <vector>

#include <jsoncons/json.hpp>
#include <mcp/export.hpp>
#include <mcp/lifecycle/session.hpp>

namespace mcp::detail
{

// Icon JSON encoding
[[nodiscard]] MCP_SDK_EXPORT auto iconToJson(const lifecycle::session::Icon &icon) -> jsoncons::json;

// Implementation JSON encoding
[[nodiscard]] MCP_SDK_EXPORT auto implementationToJson(const lifecycle::session::Implementation &implementation) -> jsoncons::json;

// Implementation JSON parsing with defaults
[[nodiscard]] MCP_SDK_EXPORT auto parseImplementation(const jsoncons::json &implementationJson, std::string defaultName, std::string defaultVersion)
  -> lifecycle::session::Implementation;

// Client capabilities JSON encoding
[[nodiscard]] MCP_SDK_EXPORT auto clientCapabilitiesToJson(const lifecycle::session::ClientCapabilities &capabilities) -> jsoncons::json;

// Server capabilities JSON encoding
[[nodiscard]] MCP_SDK_EXPORT auto serverCapabilitiesToJson(const lifecycle::session::ServerCapabilities &capabilities) -> jsoncons::json;

// Client capabilities JSON parsing
[[nodiscard]] MCP_SDK_EXPORT auto parseClientCapabilities(const jsoncons::json &capabilitiesJson) -> lifecycle::session::ClientCapabilities;

// Server capabilities JSON parsing
[[nodiscard]] MCP_SDK_EXPORT auto parseServerCapabilities(const jsoncons::json &capabilitiesJson) -> lifecycle::session::ServerCapabilities;

}  // namespace mcp::detail
