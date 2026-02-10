# Task ID: [task-037]
# Task Name: [Documentation (quickstarts, version policy, security)]

## Context
Create production-usable documentation covering server/client quickstarts, authorization, and supported protocol versions.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Documentation requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`

## Output / Definition of Done
* `docs/quickstart_server.md` covers stdio + HTTP + auth
* `docs/quickstart_client.md` covers stdio + HTTP + auth + loopback receiver
* `docs/version_policy.md` explains supported revisions and upgrade strategy
* `docs/security.md` documents origin validation defaults, token storage expectations, and safe URL handling

## Step-by-Step Instructions
1. Write quickstarts aligned to examples.
2. Document config options for TLS and origin policy.
3. Document auth flows and responsibilities (host opens browser).
4. Document limits/backpressure options.

## Verification
* Manual doc review; ensure commands in docs match CMake options
