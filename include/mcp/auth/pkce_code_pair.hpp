#pragma once

#include <cstddef>
#include <string>

namespace mcp::auth
{

inline constexpr std::size_t kDefaultPkceVerifierEntropyBytes = 32U;

struct PkceCodePair
{
  std::string codeVerifier;
  std::string codeChallenge;
  std::string codeChallengeMethod = "S256";
};

}  // namespace mcp::auth
