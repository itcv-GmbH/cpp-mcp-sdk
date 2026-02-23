#pragma once

#include <optional>
#include <string>
#include <vector>

#include <jsoncons/json.hpp>
#include <mcp/lifecycle/session.hpp>

namespace mcp::detail
{

// Icon JSON encoding
[[nodiscard]] auto iconToJson(const lifecycle::session::Icon &icon) -> jsoncons::json;

// Implementation JSON encoding
[[nodiscard]] auto implementationToJson(const lifecycle::session::Implementation &implementation) -> jsoncons::json;

// Implementation JSON parsing with defaults
[[nodiscard]] auto parseImplementation(const jsoncons::json &implementationJson, std::string defaultName, std::string defaultVersion) -> lifecycle::session::Implementation;

// Client capabilities JSON encoding
[[nodiscard]] auto clientCapabilitiesToJson(const lifecycle::session::ClientCapabilities &capabilities) -> jsoncons::json;

// Server capabilities JSON encoding
[[nodiscard]] auto serverCapabilitiesToJson(const lifecycle::session::ServerCapabilities &capabilities) -> jsoncons::json;

// Client capabilities JSON parsing
[[nodiscard]] auto parseClientCapabilities(const jsoncons::json &capabilitiesJson) -> lifecycle::session::ClientCapabilities;

// Server capabilities JSON parsing
[[nodiscard]] auto parseServerCapabilities(const jsoncons::json &capabilitiesJson) -> lifecycle::session::ServerCapabilities;

}  // namespace mcp::detail
