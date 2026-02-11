#include <exception>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/client/client.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/http.hpp>

namespace
{

struct Options
{
  std::string endpoint;
  std::optional<std::string> token;
  bool expectUnauthorized = false;
};

auto parseOptions(int argc, char **argv) -> Options
{
  Options options;

  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
  for (int index = 1; index < argc; ++index)
  {
    arguments.emplace_back(argv[index]);
  }

  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string_view argument = arguments[index];
    const auto requireValue = [&arguments, &index](std::string_view name) -> std::string
    {
      if (index + 1 >= arguments.size())
      {
        throw std::invalid_argument("Missing value for argument: " + std::string(name));
      }

      ++index;
      return arguments[index];
    };

    if (argument == "--endpoint")
    {
      options.endpoint = requireValue(argument);
      continue;
    }

    if (argument == "--token")
    {
      options.token = requireValue(argument);
      continue;
    }

    if (argument == "--expect-unauthorized")
    {
      options.expectUnauthorized = true;
      continue;
    }

    throw std::invalid_argument("Unknown argument: " + std::string(argument));
  }

  if (options.endpoint.empty())
  {
    throw std::invalid_argument("--endpoint is required");
  }

  return options;
}

auto hasTool(const std::vector<mcp::ToolDefinition> &tools, std::string_view name) -> bool
{
  for (const mcp::ToolDefinition &tool : tools)
  {
    if (tool.name == name)
    {
      return true;
    }
  }

  return false;
}

auto hasResource(const std::vector<mcp::ResourceDefinition> &resources, std::string_view uri) -> bool
{
  for (const mcp::ResourceDefinition &resource : resources)
  {
    if (resource.uri == uri)
    {
      return true;
    }
  }

  return false;
}

auto hasPrompt(const std::vector<mcp::PromptDefinition> &prompts, std::string_view name) -> bool
{
  for (const mcp::PromptDefinition &prompt : prompts)
  {
    if (prompt.name == name)
    {
      return true;
    }
  }

  return false;
}

auto firstTextContent(const mcp::CallToolResult &result) -> std::string
{
  if (!result.content.is_array() || result.content.empty())
  {
    return {};
  }

  const mcp::jsonrpc::JsonValue &entry = result.content[0];
  if (!entry.is_object() || !entry.contains("text") || !entry["text"].is_string())
  {
    return {};
  }

  return entry["text"].as<std::string>();
}

auto promptContainsTopic(const mcp::PromptGetResult &promptResult, std::string_view topic) -> bool
{
  for (const mcp::PromptMessage &message : promptResult.messages)
  {
    if (!message.content.is_object() || !message.content.contains("text") || !message.content["text"].is_string())
    {
      continue;
    }

    if (message.content["text"].as<std::string>().find(topic) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

auto initializeSucceeded(const mcp::jsonrpc::Response &response) -> bool
{
  return std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response);
}

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

    auto client = mcp::Client::create();
    mcp::transport::HttpClientOptions clientOptions;
    clientOptions.endpointUrl = options.endpoint;
    if (options.token.has_value())
    {
      clientOptions.bearerToken = options.token;
    }

    client->connectHttp(clientOptions);
    client->start();

    if (options.expectUnauthorized)
    {
      try
      {
        const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
        client->stop();
        if (initializeSucceeded(initializeResponse))
        {
          std::cerr << "Expected unauthorized initialize failure, but initialize succeeded" << '\n';
          return 2;
        }

        return 0;
      }
      catch (const std::exception &error)
      {
        client->stop();
        std::cerr << "Observed expected unauthorized failure: " << error.what() << '\n';
        return 0;
      }
    }

    const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
    if (!initializeSucceeded(initializeResponse))
    {
      std::cerr << "initialize did not return success" << '\n';
      client->stop();
      return 3;
    }

    const mcp::ListToolsResult tools = client->listTools();
    if (!hasTool(tools.tools, "python_echo"))
    {
      std::cerr << "python_echo tool was not advertised by reference server" << '\n';
      client->stop();
      return 4;
    }

    mcp::jsonrpc::JsonValue toolArguments = mcp::jsonrpc::JsonValue::object();
    toolArguments["text"] = "from-cpp";
    const mcp::CallToolResult toolResult = client->callTool("python_echo", std::move(toolArguments));
    const std::string toolText = firstTextContent(toolResult);
    if (toolText.find("from-cpp") == std::string::npos)
    {
      std::cerr << "python_echo result did not include expected text" << '\n';
      client->stop();
      return 5;
    }

    const mcp::ListResourcesResult resources = client->listResources();
    if (!hasResource(resources.resources, "resource://python-server/info"))
    {
      std::cerr << "resource://python-server/info was not advertised by reference server" << '\n';
      client->stop();
      return 6;
    }

    const mcp::ReadResourceResult resourceResult = client->readResource("resource://python-server/info");
    if (resourceResult.contents.empty() || resourceResult.contents[0].value.find("python reference server") == std::string::npos)
    {
      std::cerr << "Reference resource content was missing expected marker" << '\n';
      client->stop();
      return 7;
    }

    const mcp::ListPromptsResult prompts = client->listPrompts();
    if (!hasPrompt(prompts.prompts, "python_server_prompt"))
    {
      std::cerr << "python_server_prompt was not advertised by reference server" << '\n';
      client->stop();
      return 8;
    }

    mcp::jsonrpc::JsonValue promptArguments = mcp::jsonrpc::JsonValue::object();
    promptArguments["topic"] = "interop";
    const mcp::PromptGetResult promptResult = client->getPrompt("python_server_prompt", std::move(promptArguments));
    if (!promptContainsTopic(promptResult, "interop"))
    {
      std::cerr << "Reference prompt result did not include expected topic" << '\n';
      client->stop();
      return 9;
    }

    client->stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_client_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
