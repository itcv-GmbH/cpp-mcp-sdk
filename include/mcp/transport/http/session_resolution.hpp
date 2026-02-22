#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/transport/http/session_lookup_state.hpp>

namespace mcp::transport::http
{

struct SessionResolution
{
  SessionLookupState state = SessionLookupState::kUnknown;
  std::optional<std::string> negotiatedProtocolVersion;
};

}  // namespace mcp::transport::http