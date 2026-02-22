#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/bearer_www_authenticate_parameter.hpp>

namespace mcp::auth
{

struct BearerWwwAuthenticateChallenge
{
  std::optional<std::string> resourceMetadata;
  std::optional<std::string> scope;
  std::optional<std::string> error;
  std::vector<BearerWwwAuthenticateParameter> parameters;
};

}  // namespace mcp::auth
