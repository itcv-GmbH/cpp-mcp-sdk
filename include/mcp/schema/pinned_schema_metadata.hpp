#pragma once

#include <string>

namespace mcp::schema
{

struct PinnedSchemaMetadata
{
  std::string localPath;
  std::string upstreamSchemaUrl;
  std::string upstreamRef;
  std::string sha256;
};

}  // namespace mcp::schema
