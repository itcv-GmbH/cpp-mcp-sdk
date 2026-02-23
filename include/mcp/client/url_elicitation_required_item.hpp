#pragma once

#include <string>

namespace mcp::client
{

struct UrlElicitationRequiredItem
{
  std::string elicitationId;
  std::string message;
  std::string url;
};

}  // namespace mcp::client
