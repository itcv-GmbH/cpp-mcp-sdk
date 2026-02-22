#pragma once

#include <string>
#include <vector>

namespace mcp::transport::http
{

struct Header
{
  std::string name;
  std::string value;
};

using HeaderList = std::vector<Header>;

}  // namespace mcp::transport::http