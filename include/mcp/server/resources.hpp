#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct ResourceDefinition
{
  std::string uri;
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<jsonrpc::JsonValue> icons;
  std::optional<std::string> mimeType;
  std::optional<std::uint64_t> size;
  std::optional<jsonrpc::JsonValue> annotations;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct ResourceTemplateDefinition
{
  std::string uriTemplate;
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<jsonrpc::JsonValue> icons;
  std::optional<std::string> mimeType;
  std::optional<jsonrpc::JsonValue> annotations;
  std::optional<jsonrpc::JsonValue> metadata;
};

enum class ResourceContentKind
{
  kText,
  kBlobBase64,
};

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

struct ResourceReadContext
{
  jsonrpc::RequestContext requestContext;
  std::string uri;
};

using ResourceReadHandler = std::function<std::vector<ResourceContent>(const ResourceReadContext &)>;

struct RegisteredResource
{
  ResourceDefinition definition;
  ResourceReadHandler handler;
};

struct ResourceSubscription
{
  std::string sessionKey;
  std::string uri;
};

}  // namespace mcp
