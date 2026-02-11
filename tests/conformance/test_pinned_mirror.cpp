#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/schema/validator.hpp>

namespace
{

auto sourceTreeRoot() -> std::filesystem::path
{
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

auto readManifestEntries(const std::filesystem::path &manifestPath) -> std::vector<std::string>
{
  std::ifstream input(manifestPath);
  if (!input)
  {
    throw std::runtime_error("Failed to open manifest: " + manifestPath.string());
  }

  const std::regex manifestEntryPattern(R"(^\s*-\s*`([^`]+)`\s*<-)");

  std::vector<std::string> entries;
  std::string line;
  std::smatch match;
  while (std::getline(input, line))
  {
    if (std::regex_search(line, match, manifestEntryPattern))
    {
      entries.push_back(match[1].str());
    }
  }

  return entries;
}

}  // namespace

TEST_CASE("Pinned mirror manifest files exist", "[conformance][pinned_mirror]")
{
  const std::filesystem::path repositoryRoot = sourceTreeRoot();
  const std::filesystem::path mirrorRoot = repositoryRoot / ".docs/requirements/mcp-spec-2025-11-25";
  const std::filesystem::path manifestPath = mirrorRoot / "MANIFEST.md";

  const std::vector<std::string> manifestEntries = readManifestEntries(manifestPath);
  REQUIRE_FALSE(manifestEntries.empty());

  for (const std::string &relativePath : manifestEntries)
  {
    INFO("Missing manifest entry path: " << relativePath);
    REQUIRE(std::filesystem::exists(mirrorRoot / relativePath));
  }
}

TEST_CASE("Schema validator uses pinned mirror schema path", "[conformance][pinned_mirror][schema]")
{
  const std::filesystem::path expectedPinnedPath = sourceTreeRoot() / ".docs/requirements/mcp-spec-2025-11-25/schema/schema.json";

  const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  const std::filesystem::path configuredPinnedPath = validator.metadata().localPath;

  REQUIRE(std::filesystem::exists(configuredPinnedPath));
  REQUIRE(std::filesystem::equivalent(configuredPinnedPath, expectedPinnedPath));
}
