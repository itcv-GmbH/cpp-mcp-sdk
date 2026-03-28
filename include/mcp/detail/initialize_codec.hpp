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
MCP_SDK_EXPORT [[nodiscard]] auto iconToJson(const lifecycle::session::Icon &icon) -> jsoncons::json;

// Implementation JSON encoding
MCP_SDK_EXPORT [[nodiscard]] auto implementationToJson(const lifecycle::session::Implementation &implementation) -> jsoncons::json;

// Implementation JSON parsing with defaults
MCP_SDK_EXPORT [[nodiscard]] auto parseImplementation(const jsoncons::json &implementationJson, std::string defaultName, std::string defaultVersion) -> lifecycle::session::Implementation;

// Client capabilities JSON encoding
MCP_SDK_EXPORT [[nodiscard]] auto clientCapabilitiesToJson(const lifecycle::session::ClientCapabilities &capabilities) -> jsoncons::json;

// Server capabilities JSON encoding
MCP_SDK_EXPORT [[nodiscard]] auto serverCapabilitiesToJson(const lifecycle::session::ServerCapabilities &capabilities) -> jsoncons::json;

// Client capabilities JSON parsing
MCP_SDK_EXPORT [[nodiscard]] auto parseClientCapabilities(const jsoncons::json &capabilitiesJson) -> lifecycle::session::ClientCapabilities;

// Server capabilities JSON parsing
MCP_SDK_EXPORT [[nodiscard]] auto parseServerCapabilities(const jsoncons::json &capabilitiesJson) -> lifecycle::session::ServerCapabilities;

}  // namespace mcp::detail
