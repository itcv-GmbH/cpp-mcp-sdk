# Security Notes and Defaults

This document captures security-relevant defaults and knobs in the current SDK.

## Origin validation defaults (HTTP server)

`mcp::security::OriginPolicy` defaults (`include/mcp/security/origin_policy.hpp`):

- `validateOrigin = true`
- `allowRequestsWithoutOrigin = true`
- `allowedHosts = localhost, 127.0.0.1, ::1`

Effect:

- If an `Origin` header is present and not allowed, request validation fails with HTTP `403`.
- If `Origin` is absent, requests are accepted by default.

For stricter deployments, set:

- `allowRequestsWithoutOrigin = false`
- explicit `allowedOrigins` and/or `allowedHosts`

`HttpEndpointConfig.bindLocalhostOnly` defaults to `true`, so server runtime binds loopback unless you override it.

## Token storage expectations (OAuth clients)

The SDK exposes `mcp::auth::OAuthTokenStorage` with `load(resource)` / `save(resource, token)`.

- Default storage in step-up helpers is `InMemoryOAuthTokenStorage`.
- In-memory storage is process-local and non-persistent.

Production expectation:

- provide your own `OAuthTokenStorage` implementation backed by OS-protected secure storage
- avoid writing bearer tokens to logs, crash dumps, or URL query parameters

## Safe URL handling

### OAuth redirect URI validation

`validateRedirectUriForAuthorizationCodeFlow` enforces:

- `https://...` redirect URIs, or
- `http://localhost...` loopback redirect URIs

### OAuth authorization/token request construction

- Authorization and token endpoints must be HTTPS.
- Additional parameters that look token-bearing are rejected.
- Token exchange uses `application/x-www-form-urlencoded` request bodies (not query tokens).

### URL elicitation input validation

For `elicitation/create` URL mode, the client validates that `params.url` is a syntactically valid absolute URL and rejects malformed/userinfo-containing values.

Application-side expectation: do not auto-open URL-elicitation links without explicit user consent.

## SSRF and redirect policy

### Discovery (`discoverAuthorizationMetadata`) defaults

`DiscoverySecurityPolicy` defaults:

- `requireHttps = true`
- `requireSameOriginRedirects = true`
- `allowPrivateAndLocalAddresses = false`
- `maxRedirects = 4`

The default policy blocks:

- localhost and private/local IP targets for discovery fetches
- HTTPS downgrade redirects
- cross-origin redirects (unless explicitly allowed)

### Token request execution defaults

`OAuthHttpSecurityPolicy` defaults:

- `requireHttps = true`
- `requireSameOriginRedirects = true`
- `maxRedirects = 2`

Additional hardening:

- strips `Authorization` on cross-origin redirects
- rejects redirects that can rewrite credential-bearing methods (for example, `POST` to `GET` rewriting classes)

## Runtime limits and backpressure knobs

`mcp::security::RuntimeLimits` (`include/mcp/security/limits.hpp`) defaults:

- `maxMessageSizeBytes = 1 MiB`
- `maxConcurrentInFlightRequests = 1024`
- `maxSseStreamDuration = 30 minutes`
- `maxSseBufferedMessages = 1024`
- `maxRetryAttempts = 64`
- `maxRetryDelayMilliseconds = 60000`
- `maxTaskTtlMilliseconds = 24 hours`
- `maxConcurrentTasksPerAuthContext = 128`

Where limits apply:

- stdio and HTTP message parsing/size limits
- router in-flight request limits
- Streamable HTTP SSE buffering and reconnect backpressure
- task store TTL and per-auth-context active-task caps

## Minimal hardening checklist

- Keep HTTPS requirements enabled for OAuth discovery and token exchange.
- Keep private/local-address blocking enabled outside localhost-only test setups.
- Require `Origin` for browser-facing HTTP deployments and configure allowlists.
- Replace in-memory token storage with secure persistent storage.
- Tune runtime limits to expected traffic and payload sizes.
