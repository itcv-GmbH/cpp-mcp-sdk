#pragma once

#include <cstdint>

namespace mcp::auth
{

enum class ClientRegistrationStrategy : std::uint8_t
{
  kPreRegistered,
  kClientIdMetadataDocument,
  kDynamic,
};

}  // namespace mcp::auth
