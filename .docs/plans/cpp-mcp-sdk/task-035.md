# Task ID: [task-035]
# Task Name: [Integration Tests with Reference SDKs (Optional)]

## Context
Validate interoperability against official SDKs to reduce ecosystem integration risk.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Integration tests SHOULD include reference client/server)
* Official reference SDKs (TypeScript and/or Python)

## Output / Definition of Done
* `tests/integration/` contains scripts and fixtures to:
  - run SDK server and connect via reference client
  - run reference server and connect via SDK client
  - verify Streamable HTTP + authorization works end-to-end

## Step-by-Step Instructions
1. Choose reference SDK(s) and pin versions.
2. Add harness scripts (Python/Node) to stand up the reference side.
3. Add test cases aligned with acceptance criteria (initialize, tools, resources, prompts; sampling/elicitation bidirectional).
4. Gate tests behind a CMake option for CI environments.

## Verification
* `cmake -S . -B build -DMCP_SDK_INTEGRATION_TESTS=ON`
* `ctest --test-dir build -R integration`
