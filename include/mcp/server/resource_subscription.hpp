#pragma once

#include <string>

namespace mcp::server
{

struct ResourceSubscription
{
  std::string sessionKey;
  std::string uri;
};

}  // namespace mcp::server
