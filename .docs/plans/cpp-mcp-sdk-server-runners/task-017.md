# Task ID: task-017
# Task Name: Add Reference Interop Tests for STDIO Runner

## Context
The repository integration suite validates interoperability between this C++ SDK and the pinned Python reference SDK. This feature adds a first-class STDIO server runner. The integration suite must include a Python-driven end-to-end test that spawns a C++ STDIO server fixture and exercises initialize, tools, resources, prompts, and server-initiated requests over STDIO framing.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (STDIO transport requirements; stdout framing constraints)
* `tests/integration/README.md`
* `tests/integration/CMakeLists.txt`
* `tests/integration/scripts/setup_reference_python_sdk.py`
* `tests/integration/fixtures/reference_python_requirements.txt` (pinned `mcp==1.26.0`)
* `include/mcp/server/stdio_runner.hpp` (from `task-003`)
* `include/mcp/server/server.hpp`

## Output / Definition of Done
* `tests/integration/cpp_stdio_server_fixture.cpp` is added implementing a STDIO-only C++ MCP server fixture that:
  * uses `mcp::StdioServerRunner` with in-process `mcp::Server`
  * registers at least one tool, one resource, and one prompt with deterministic outputs
  * issues at least one server-initiated request (`sampling/createMessage` and `elicitation/create`) after `notifications/initialized` and enforces timeouts
  * writes only MCP JSON-RPC frames to stdout and writes diagnostics to stderr
* `tests/integration/scripts/reference_client_to_cpp_stdio_server.py` is added and:
  * provisions a `ClientSession` from the pinned Python reference SDK over stdio streams connected to the spawned C++ fixture subprocess
  * verifies initialize succeeds
  * verifies tool call, resource read, and prompt get succeed and return expected markers
  * handles server-initiated `sampling/createMessage` and `elicitation/create` requests and returns deterministic responses that the C++ fixture asserts
* `tests/integration/CMakeLists.txt` is updated to:
  * add an executable target `mcp_sdk_test_integration_cpp_stdio_server`
  * add a CTest entry `mcp_sdk_integration_reference_client_to_cpp_stdio_server` that runs the new python script
  * set `DEPENDS mcp_sdk_integration_reference_setup_python_sdk`
  * set `LABELS "integration;integration_reference"`

## Step-by-Step Instructions
1. Implement `tests/integration/cpp_stdio_server_fixture.cpp`:
   1. Parse no arguments.
   2. Construct a `ServerFactory` that returns a configured `std::shared_ptr<mcp::Server>` with deterministic capabilities and registrations.
   3. Configure the server to:
      - register a tool `cpp_echo` that returns a text marker containing a supplied argument
      - register a resource `resource://cpp-stdio-server/info` that returns a text marker
      - register a prompt `cpp_stdio_server_prompt` that returns a text marker containing an input topic
      - start server-initiated `sampling/createMessage` and `elicitation/create` requests after `notifications/initialized` and assert deterministic results within timeouts
   4. Construct `mcp::StdioServerRunner` with stream defaults (`std::cin`, `std::cout`, `std::cerr`) and run it in the foreground.
   5. Ensure the fixture process exits with non-zero status if any assertion fails.
2. Implement `tests/integration/scripts/reference_client_to_cpp_stdio_server.py`:
   1. Spawn the C++ stdio server fixture subprocess using `subprocess.Popen`.
   2. Connect the Python reference SDK client to the subprocess stdio streams.
      - The implementation is required to import and use the stdio client transport API provided by `mcp==1.26.0`.
      - The implementation is required to validate the selected API by importing it from the provisioned venv created by `setup_reference_python_sdk.py`.
   3. Create a `ClientSession` and call `initialize()`.
   4. Validate:
      - `list_tools()` includes `cpp_echo`
      - `call_tool("cpp_echo", ...)` returns content containing the expected marker
      - `list_resources()` includes `resource://cpp-stdio-server/info` and `read_resource(...)` returns the expected marker
      - `list_prompts()` includes `cpp_stdio_server_prompt` and `get_prompt(...)` returns the expected marker
   5. Provide callbacks/handlers for server-initiated `sampling/createMessage` and `elicitation/create` that return deterministic responses that match fixture assertions.
   6. Stop the subprocess by closing stdin and waiting for exit; the script must fail on non-zero exit codes and must print a single JSON object success marker on success.
3. Update `tests/integration/CMakeLists.txt`:
   1. Add `add_executable(mcp_sdk_test_integration_cpp_stdio_server cpp_stdio_server_fixture.cpp)` and link it to `mcp::sdk`.
   2. Add `add_test(NAME mcp_sdk_integration_reference_client_to_cpp_stdio_server COMMAND "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_stdio_server.py" --cpp-server "$<TARGET_FILE:mcp_sdk_test_integration_cpp_stdio_server>")`.
   3. Set test properties:
      - `DEPENDS mcp_sdk_integration_reference_setup_python_sdk`
      - `ENVIRONMENT "PYTHONUNBUFFERED=1"`
      - `LABELS "integration;integration_reference"`
      - `TIMEOUT 300`
4. Update `tests/integration/README.md`:
   1. Add coverage text stating that Python reference client -> C++ stdio server runner is exercised.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON && cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R integration_reference_client_to_cpp_stdio_server --output-on-failure`
