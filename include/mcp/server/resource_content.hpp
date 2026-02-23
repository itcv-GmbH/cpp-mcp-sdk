#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/resource_content_kind.hpp>

namespace mcp::server
{

struct ResourceContent
{
  std::string uri;
  std::optional<std::string> mimeType;
  ResourceContentKind kind = ResourceContentKind::kText;
  std::string value;
  std::optional<jsonrpc::JsonValue> annotations;
  std::optional<jsonrpc::JsonValue> metadata;

  static auto text(std::string uri,
                   std::string text,
                   std::optional<std::string> mimeType = std::nullopt,
                   std::optional<jsonrpc::JsonValue> annotations = std::nullopt,
                   std::optional<jsonrpc::JsonValue> metadata = std::nullopt) -> ResourceContent;

  static auto blobBase64(std::string uri,
                         std::string blobBase64,
                         std::optional<std::string> mimeType = std::nullopt,
                         std::optional<jsonrpc::JsonValue> annotations = std::nullopt,
                         std::optional<jsonrpc::JsonValue> metadata = std::nullopt) -> ResourceContent;

  static auto blobBytes(std::string uri,
                        const std::vector<std::uint8_t> &blobBytes,
                        std::optional<std::string> mimeType = std::nullopt,
                        std::optional<jsonrpc::JsonValue> annotations = std::nullopt,
                        std::optional<jsonrpc::JsonValue> metadata = std::nullopt) -> ResourceContent;
};

}  // namespace mcp::server
