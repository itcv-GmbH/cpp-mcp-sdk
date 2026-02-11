#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct PromptArgumentDefinition
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<bool> required;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct PromptDefinition
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<jsonrpc::JsonValue> icons;
  std::vector<PromptArgumentDefinition> arguments;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct PromptMessage
{
  std::string role;
  jsonrpc::JsonValue content = jsonrpc::JsonValue::object();
};

struct PromptGetResult
{
  std::optional<std::string> description;
  std::vector<PromptMessage> messages;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct PromptGetContext
{
  jsonrpc::RequestContext requestContext;
  std::string promptName;
  jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object();
};

using PromptHandler = std::function<PromptGetResult(const PromptGetContext &)>;

struct RegisteredPrompt
{
  PromptDefinition definition;
  PromptHandler handler;
};

}  // namespace mcp
