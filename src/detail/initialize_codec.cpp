#include <optional>
#include <string>
#include <vector>

#include <jsoncons/json.hpp>
#include <mcp/detail/initialize_codec.hpp>
#include <mcp/lifecycle/session.hpp>

namespace mcp
{

namespace detail
{

namespace
{

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto parseIcon(const jsoncons::json &iconJson) -> std::optional<Icon>
{
  if (!iconJson.is_object() || !iconJson.contains("src") || !iconJson["src"].is_string())
  {
    return std::nullopt;
  }

  std::optional<std::string> mimeType;
  if (iconJson.contains("mimeType") && iconJson["mimeType"].is_string())
  {
    mimeType = iconJson["mimeType"].as<std::string>();
  }

  std::optional<std::vector<std::string>> sizes;
  if (iconJson.contains("sizes") && iconJson["sizes"].is_array())
  {
    std::vector<std::string> parsedSizes;
    for (const auto &sizeValue : iconJson["sizes"].array_range())
    {
      if (sizeValue.is_string())
      {
        parsedSizes.push_back(sizeValue.as<std::string>());
      }
    }

    sizes = std::move(parsedSizes);
  }

  std::optional<std::string> theme;
  if (iconJson.contains("theme") && iconJson["theme"].is_string())
  {
    theme = iconJson["theme"].as<std::string>();
  }

  return Icon {iconJson["src"].as<std::string>(), std::move(mimeType), std::move(sizes), std::move(theme)};
}

}  // namespace

auto iconToJson(const Icon &icon) -> jsoncons::json
{
  jsoncons::json iconJson = jsoncons::json::object();
  iconJson["src"] = icon.src();

  if (icon.mimeType().has_value())
  {
    iconJson["mimeType"] = *icon.mimeType();
  }

  if (icon.sizes().has_value())
  {
    iconJson["sizes"] = jsoncons::json::array(icon.sizes()->begin(), icon.sizes()->end());
  }

  if (icon.theme().has_value())
  {
    iconJson["theme"] = *icon.theme();
  }

  return iconJson;
}

auto implementationToJson(const Implementation &implementation) -> jsoncons::json
{
  jsoncons::json implementationJson = jsoncons::json::object();
  implementationJson["name"] = implementation.name();
  implementationJson["version"] = implementation.version();

  if (implementation.title().has_value())
  {
    implementationJson["title"] = *implementation.title();
  }

  if (implementation.description().has_value())
  {
    implementationJson["description"] = *implementation.description();
  }

  if (implementation.websiteUrl().has_value())
  {
    implementationJson["websiteUrl"] = *implementation.websiteUrl();
  }

  if (implementation.icons().has_value())
  {
    jsoncons::json iconsJson = jsoncons::json::array();
    for (const auto &icon : *implementation.icons())
    {
      iconsJson.push_back(iconToJson(icon));
    }

    implementationJson["icons"] = std::move(iconsJson);
  }

  return implementationJson;
}

auto parseImplementation(const jsoncons::json &implementationJson, std::string defaultName, std::string defaultVersion) -> Implementation
{
  if (!implementationJson.is_object())
  {
    return {std::move(defaultName), std::move(defaultVersion)};
  }

  std::string name = implementationJson.contains("name") && implementationJson["name"].is_string() ? implementationJson["name"].as<std::string>() : std::move(defaultName);

  std::string version =
    implementationJson.contains("version") && implementationJson["version"].is_string() ? implementationJson["version"].as<std::string>() : std::move(defaultVersion);

  std::optional<std::string> title;
  if (implementationJson.contains("title") && implementationJson["title"].is_string())
  {
    title = implementationJson["title"].as<std::string>();
  }

  std::optional<std::string> description;
  if (implementationJson.contains("description") && implementationJson["description"].is_string())
  {
    description = implementationJson["description"].as<std::string>();
  }

  std::optional<std::string> websiteUrl;
  if (implementationJson.contains("websiteUrl") && implementationJson["websiteUrl"].is_string())
  {
    websiteUrl = implementationJson["websiteUrl"].as<std::string>();
  }

  std::optional<std::vector<Icon>> icons;
  if (implementationJson.contains("icons") && implementationJson["icons"].is_array())
  {
    std::vector<Icon> parsedIcons;
    for (const auto &iconValue : implementationJson["icons"].array_range())
    {
      const auto parsedIcon = parseIcon(iconValue);
      if (parsedIcon.has_value())
      {
        parsedIcons.push_back(*parsedIcon);
      }
    }

    icons = std::move(parsedIcons);
  }

  return {std::move(name), std::move(version), std::move(title), std::move(description), std::move(websiteUrl), std::move(icons)};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto clientCapabilitiesToJson(const ClientCapabilities &capabilities) -> jsoncons::json
{
  jsoncons::json capabilitiesJson = jsoncons::json::object();

  if (capabilities.experimental().has_value())
  {
    capabilitiesJson["experimental"] = *capabilities.experimental();
  }

  if (capabilities.roots().has_value())
  {
    jsoncons::json rootsJson = jsoncons::json::object();
    rootsJson["listChanged"] = capabilities.roots()->listChanged;
    capabilitiesJson["roots"] = std::move(rootsJson);
  }

  if (capabilities.sampling().has_value())
  {
    jsoncons::json samplingJson = jsoncons::json::object();
    if (capabilities.sampling()->context)
    {
      samplingJson["context"] = jsoncons::json::object();
    }

    if (capabilities.sampling()->tools)
    {
      samplingJson["tools"] = jsoncons::json::object();
    }

    capabilitiesJson["sampling"] = std::move(samplingJson);
  }

  if (capabilities.elicitation().has_value())
  {
    jsoncons::json elicitationJson = jsoncons::json::object();
    if (capabilities.elicitation()->form)
    {
      elicitationJson["form"] = jsoncons::json::object();
    }

    if (capabilities.elicitation()->url)
    {
      elicitationJson["url"] = jsoncons::json::object();
    }

    capabilitiesJson["elicitation"] = std::move(elicitationJson);
  }

  if (capabilities.tasks().has_value())
  {
    jsoncons::json tasksJson = jsoncons::json::object();
    if (capabilities.tasks()->list)
    {
      tasksJson["list"] = jsoncons::json::object();
    }

    if (capabilities.tasks()->cancel)
    {
      tasksJson["cancel"] = jsoncons::json::object();
    }

    if (capabilities.tasks()->samplingCreateMessage || capabilities.tasks()->elicitationCreate || capabilities.tasks()->toolsCall)
    {
      jsoncons::json requestsJson = jsoncons::json::object();
      if (capabilities.tasks()->samplingCreateMessage)
      {
        jsoncons::json samplingJson = jsoncons::json::object();
        samplingJson["createMessage"] = jsoncons::json::object();
        requestsJson["sampling"] = std::move(samplingJson);
      }

      if (capabilities.tasks()->elicitationCreate)
      {
        jsoncons::json elicitationJson = jsoncons::json::object();
        elicitationJson["create"] = jsoncons::json::object();
        requestsJson["elicitation"] = std::move(elicitationJson);
      }

      if (capabilities.tasks()->toolsCall)
      {
        jsoncons::json toolsJson = jsoncons::json::object();
        toolsJson["call"] = jsoncons::json::object();
        requestsJson["tools"] = std::move(toolsJson);
      }

      tasksJson["requests"] = std::move(requestsJson);
    }

    capabilitiesJson["tasks"] = std::move(tasksJson);
  }

  return capabilitiesJson;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto parseClientCapabilities(const jsoncons::json &capabilitiesJson) -> ClientCapabilities
{
  if (!capabilitiesJson.is_object())
  {
    return ClientCapabilities {};
  }

  std::optional<RootsCapability> roots;
  if (capabilitiesJson.contains("roots") && capabilitiesJson["roots"].is_object())
  {
    RootsCapability rootsCapability;
    if (capabilitiesJson["roots"].contains("listChanged") && capabilitiesJson["roots"]["listChanged"].is_bool())
    {
      rootsCapability.listChanged = capabilitiesJson["roots"]["listChanged"].as<bool>();
    }

    roots = rootsCapability;
  }

  std::optional<SamplingCapability> sampling;
  if (capabilitiesJson.contains("sampling") && capabilitiesJson["sampling"].is_object())
  {
    SamplingCapability samplingCapability;
    samplingCapability.context = capabilitiesJson["sampling"].contains("context") && capabilitiesJson["sampling"]["context"].is_object();
    samplingCapability.tools = capabilitiesJson["sampling"].contains("tools") && capabilitiesJson["sampling"]["tools"].is_object();
    sampling = samplingCapability;
  }

  std::optional<ElicitationCapability> elicitation;
  if (capabilitiesJson.contains("elicitation") && capabilitiesJson["elicitation"].is_object())
  {
    const auto &elicitationJson = capabilitiesJson["elicitation"];
    ElicitationCapability elicitationCapability;
    elicitationCapability.form = elicitationJson.contains("form") && elicitationJson["form"].is_object();
    elicitationCapability.url = elicitationJson.contains("url") && elicitationJson["url"].is_object();
    if (!elicitationCapability.form && !elicitationCapability.url && elicitationJson.empty())
    {
      elicitationCapability.form = true;
    }

    elicitation = elicitationCapability;
  }

  std::optional<TasksCapability> tasks;
  if (capabilitiesJson.contains("tasks") && capabilitiesJson["tasks"].is_object())
  {
    TasksCapability tasksCapability;
    const auto &tasksJson = capabilitiesJson["tasks"];
    tasksCapability.list = tasksJson.contains("list") && tasksJson["list"].is_object();
    tasksCapability.cancel = tasksJson.contains("cancel") && tasksJson["cancel"].is_object();

    if (tasksJson.contains("requests") && tasksJson["requests"].is_object())
    {
      const auto &requestsJson = tasksJson["requests"];
      if (requestsJson.contains("sampling") && requestsJson["sampling"].is_object())
      {
        tasksCapability.samplingCreateMessage = requestsJson["sampling"].contains("createMessage") && requestsJson["sampling"]["createMessage"].is_object();
      }

      if (requestsJson.contains("elicitation") && requestsJson["elicitation"].is_object())
      {
        tasksCapability.elicitationCreate = requestsJson["elicitation"].contains("create") && requestsJson["elicitation"]["create"].is_object();
      }
    }

    tasks = tasksCapability;
  }

  std::optional<jsoncons::json> experimental;
  if (capabilitiesJson.contains("experimental") && capabilitiesJson["experimental"].is_object())
  {
    experimental = capabilitiesJson["experimental"];
  }

  return {roots, sampling, elicitation, tasks, std::move(experimental)};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto serverCapabilitiesToJson(const ServerCapabilities &capabilities) -> jsoncons::json
{
  jsoncons::json capabilitiesJson = jsoncons::json::object();

  if (capabilities.experimental().has_value())
  {
    capabilitiesJson["experimental"] = *capabilities.experimental();
  }

  if (capabilities.logging().has_value())
  {
    capabilitiesJson["logging"] = jsoncons::json::object();
  }

  if (capabilities.completions().has_value())
  {
    capabilitiesJson["completions"] = jsoncons::json::object();
  }

  if (capabilities.prompts().has_value())
  {
    jsoncons::json promptsJson = jsoncons::json::object();
    if (capabilities.prompts()->listChanged)
    {
      promptsJson["listChanged"] = true;
    }

    capabilitiesJson["prompts"] = std::move(promptsJson);
  }

  if (capabilities.resources().has_value())
  {
    jsoncons::json resourcesJson = jsoncons::json::object();
    if (capabilities.resources()->subscribe)
    {
      resourcesJson["subscribe"] = true;
    }

    if (capabilities.resources()->listChanged)
    {
      resourcesJson["listChanged"] = true;
    }

    capabilitiesJson["resources"] = std::move(resourcesJson);
  }

  if (capabilities.tools().has_value())
  {
    jsoncons::json toolsJson = jsoncons::json::object();
    if (capabilities.tools()->listChanged)
    {
      toolsJson["listChanged"] = true;
    }

    capabilitiesJson["tools"] = std::move(toolsJson);
  }

  if (capabilities.tasks().has_value())
  {
    jsoncons::json tasksJson = jsoncons::json::object();
    if (capabilities.tasks()->list)
    {
      tasksJson["list"] = jsoncons::json::object();
    }

    if (capabilities.tasks()->cancel)
    {
      tasksJson["cancel"] = jsoncons::json::object();
    }

    if (capabilities.tasks()->toolsCall)
    {
      jsoncons::json requestsJson = jsoncons::json::object();
      jsoncons::json toolsJson = jsoncons::json::object();
      toolsJson["call"] = jsoncons::json::object();
      requestsJson["tools"] = std::move(toolsJson);
      tasksJson["requests"] = std::move(requestsJson);
    }

    capabilitiesJson["tasks"] = std::move(tasksJson);
  }

  return capabilitiesJson;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto parseServerCapabilities(const jsoncons::json &capabilitiesJson) -> ServerCapabilities
{
  if (!capabilitiesJson.is_object())
  {
    return ServerCapabilities {};
  }

  std::optional<LoggingCapability> logging;
  if (capabilitiesJson.contains("logging") && capabilitiesJson["logging"].is_object())
  {
    logging = LoggingCapability {};
  }

  std::optional<CompletionsCapability> completions;
  if (capabilitiesJson.contains("completions") && capabilitiesJson["completions"].is_object())
  {
    completions = CompletionsCapability {};
  }

  std::optional<PromptsCapability> prompts;
  if (capabilitiesJson.contains("prompts") && capabilitiesJson["prompts"].is_object())
  {
    PromptsCapability promptsCapability;
    if (capabilitiesJson["prompts"].contains("listChanged") && capabilitiesJson["prompts"]["listChanged"].is_bool())
    {
      promptsCapability.listChanged = capabilitiesJson["prompts"]["listChanged"].as<bool>();
    }

    prompts = promptsCapability;
  }

  std::optional<ResourcesCapability> resources;
  if (capabilitiesJson.contains("resources") && capabilitiesJson["resources"].is_object())
  {
    ResourcesCapability resourcesCapability;
    if (capabilitiesJson["resources"].contains("subscribe") && capabilitiesJson["resources"]["subscribe"].is_bool())
    {
      resourcesCapability.subscribe = capabilitiesJson["resources"]["subscribe"].as<bool>();
    }

    if (capabilitiesJson["resources"].contains("listChanged") && capabilitiesJson["resources"]["listChanged"].is_bool())
    {
      resourcesCapability.listChanged = capabilitiesJson["resources"]["listChanged"].as<bool>();
    }

    resources = resourcesCapability;
  }

  std::optional<ToolsCapability> tools;
  if (capabilitiesJson.contains("tools") && capabilitiesJson["tools"].is_object())
  {
    ToolsCapability toolsCapability;
    if (capabilitiesJson["tools"].contains("listChanged") && capabilitiesJson["tools"]["listChanged"].is_bool())
    {
      toolsCapability.listChanged = capabilitiesJson["tools"]["listChanged"].as<bool>();
    }

    tools = toolsCapability;
  }

  std::optional<TasksCapability> tasks;
  if (capabilitiesJson.contains("tasks") && capabilitiesJson["tasks"].is_object())
  {
    TasksCapability tasksCapability;
    const auto &tasksJson = capabilitiesJson["tasks"];
    tasksCapability.list = tasksJson.contains("list") && tasksJson["list"].is_object();
    tasksCapability.cancel = tasksJson.contains("cancel") && tasksJson["cancel"].is_object();

    if (tasksJson.contains("requests") && tasksJson["requests"].is_object())
    {
      const auto &requestsJson = tasksJson["requests"];
      if (requestsJson.contains("tools") && requestsJson["tools"].is_object())
      {
        tasksCapability.toolsCall = requestsJson["tools"].contains("call") && requestsJson["tools"]["call"].is_object();
      }
    }

    tasks = tasksCapability;
  }

  std::optional<jsoncons::json> experimental;
  if (capabilitiesJson.contains("experimental") && capabilitiesJson["experimental"].is_object())
  {
    experimental = capabilitiesJson["experimental"];
  }

  return {logging, completions, prompts, resources, tools, tasks, std::move(experimental)};
}

}  // namespace detail

}  // namespace mcp
