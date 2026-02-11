#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mcp/auth/protected_resource_metadata.hpp>

namespace mcp::auth
{

enum class ClientRegistrationErrorCode : std::uint8_t
{
  kInvalidInput,
  kNetworkFailure,
  kMetadataValidation,
  kHostInteractionRequired,
};

class ClientRegistrationError : public std::runtime_error
{
public:
  ClientRegistrationError(ClientRegistrationErrorCode code, const std::string &message);

  [[nodiscard]] auto code() const noexcept -> ClientRegistrationErrorCode;

private:
  ClientRegistrationErrorCode code_;
};

enum class ClientAuthenticationMethod : std::uint8_t
{
  kNone,
  kClientSecretBasic,
  kClientSecretPost,
  kPrivateKeyJwt,
};

struct ResolvedClientIdentity
{
  std::string clientId;
  std::vector<std::string> redirectUris;
  ClientAuthenticationMethod authenticationMethod = ClientAuthenticationMethod::kNone;
  std::optional<std::string> clientSecret;
};

struct PreRegisteredClientConfiguration
{
  std::string clientId;
  std::vector<std::string> redirectUris;
  ClientAuthenticationMethod authenticationMethod = ClientAuthenticationMethod::kNone;
  std::optional<std::string> clientSecret;
};

struct ClientIdMetadataDocumentConfiguration
{
  std::string clientId;
  std::string clientName;
  std::vector<std::string> redirectUris;
  ClientAuthenticationMethod authenticationMethod = ClientAuthenticationMethod::kNone;
  std::optional<std::string> clientUri;
  std::optional<std::string> logoUri;
};

struct DynamicClientRegistrationConfiguration
{
  bool enabled = true;
  std::optional<std::string> clientName;
  std::vector<std::string> redirectUris;
  ClientAuthenticationMethod authenticationMethod = ClientAuthenticationMethod::kNone;
};

struct ClientRegistrationStrategyConfiguration
{
  std::optional<PreRegisteredClientConfiguration> preRegistered;
  std::optional<ClientIdMetadataDocumentConfiguration> clientIdMetadataDocument;
  DynamicClientRegistrationConfiguration dynamic;
};

enum class ClientRegistrationStrategy : std::uint8_t
{
  kPreRegistered,
  kClientIdMetadataDocument,
  kDynamic,
};

struct ClientRegistrationResult
{
  ClientRegistrationStrategy strategy = ClientRegistrationStrategy::kPreRegistered;
  ResolvedClientIdentity client;
};

struct ClientRegistrationHeader
{
  std::string name;
  std::string value;
};

struct ClientRegistrationHttpRequest
{
  std::string method = "POST";
  std::string url;
  std::vector<ClientRegistrationHeader> headers;
  std::string body;
};

struct ClientRegistrationHttpResponse
{
  std::uint16_t statusCode = 0;
  std::vector<ClientRegistrationHeader> headers;
  std::string body;
};

using ClientRegistrationHttpExecutor = std::function<ClientRegistrationHttpResponse(const ClientRegistrationHttpRequest &)>;

class ClientCredentialsStore
{
public:
  ClientCredentialsStore() = default;
  ClientCredentialsStore(const ClientCredentialsStore &) = default;
  auto operator=(const ClientCredentialsStore &) -> ClientCredentialsStore & = default;
  ClientCredentialsStore(ClientCredentialsStore &&) noexcept = default;
  auto operator=(ClientCredentialsStore &&) noexcept -> ClientCredentialsStore & = default;
  virtual ~ClientCredentialsStore() = default;
  virtual auto load(std::string_view authorizationServerIssuer) const -> std::optional<ResolvedClientIdentity> = 0;
  virtual auto save(std::string authorizationServerIssuer, ResolvedClientIdentity identity) -> void = 0;
};

class InMemoryClientCredentialsStore final : public ClientCredentialsStore
{
public:
  auto load(std::string_view authorizationServerIssuer) const -> std::optional<ResolvedClientIdentity> override;
  auto save(std::string authorizationServerIssuer, ResolvedClientIdentity identity) -> void override;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ResolvedClientIdentity> credentialsByIssuer_;
};

struct ResolveClientRegistrationRequest
{
  AuthorizationServerMetadata authorizationServerMetadata;
  ClientRegistrationStrategyConfiguration strategyConfiguration;
  std::shared_ptr<ClientCredentialsStore> credentialsStore = std::make_shared<InMemoryClientCredentialsStore>();
  ClientRegistrationHttpExecutor httpExecutor;
};

auto validateClientIdMetadataDocumentClientIdUrl(std::string_view clientIdUrl) -> void;
auto buildClientIdMetadataDocumentPayload(const ClientIdMetadataDocumentConfiguration &configuration) -> std::string;
auto validateClientIdMetadataDocumentPayload(std::string_view metadataDocumentJson, std::string_view expectedClientIdUrl) -> void;
auto resolveClientRegistration(const ResolveClientRegistrationRequest &request) -> ClientRegistrationResult;

}  // namespace mcp::auth
