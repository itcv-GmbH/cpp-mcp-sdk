#pragma once

#include <optional>
#include <string>
#include <vector>

#include <jsoncons/json.hpp>
#include <mcp/lifecycle/session.hpp>

namespace mcp::detail
{

// Icon JSON encoding
[[nodiscard]] auto iconToJson(const Icon &icon) -> jsoncons::json;

// Implementation JSON encoding
[[nodiscard]] auto implementationToJson(const Implementation &implementation) -> jsoncons::json;

// Implementation JSON parsing with defaults
[[nodiscard]] auto parseImplementation(const jsoncons::json &implementationJson, std::string defaultName, std::string defaultVersion) -> Implementation;

// Client capabilities JSON encoding
[[nodiscard]] auto clientCapabilitiesToJson(const ClientCapabilities &capabilities) -> jsoncons::json;

// Server capabilities JSON encoding
[[nodiscard]] auto serverCapabilitiesToJson(const ServerCapabilities &capabilities) -> jsoncons::json;

// Client capabilities JSON parsing
[[nodiscard]] auto parseClientCapabilities(const jsoncons::json &capabilitiesJson) -> ClientCapabilities;

// Server capabilities JSON parsing
[[nodiscard]] auto parseServerCapabilities(const jsoncons::json &capabilitiesJson) -> ServerCapabilities;

}  // namespace mcp::detail
