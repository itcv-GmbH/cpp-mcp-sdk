# Tests CMakeLists.txt

# Exclude unit test targets from clang-tidy; keep analysis focused on SDK sources.
set(CMAKE_CXX_CLANG_TIDY "")

add_executable(mcp_sdk_test_smoke smoke_test.cpp)

target_link_libraries(mcp_sdk_test_smoke
    PRIVATE
        mcp::sdk
)

add_executable(mcp_sdk_test_jsonrpc_messages jsonrpc_messages_test.cpp)

target_link_libraries(mcp_sdk_test_jsonrpc_messages
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

# Register with CTest
add_test(NAME mcp_sdk_smoke_test COMMAND mcp_sdk_test_smoke)
add_test(NAME mcp_sdk_jsonrpc_messages_test COMMAND mcp_sdk_test_jsonrpc_messages)

add_executable(mcp_sdk_test_jsonrpc_router jsonrpc_router_test.cpp)

target_link_libraries(mcp_sdk_test_jsonrpc_router
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_jsonrpc_router_test COMMAND mcp_sdk_test_jsonrpc_router)

add_executable(mcp_sdk_test_schema_validator schema_validator_test.cpp)

target_link_libraries(mcp_sdk_test_schema_validator
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_schema_validator_test COMMAND mcp_sdk_test_schema_validator)

add_executable(mcp_sdk_test_lifecycle lifecycle_test.cpp)

target_link_libraries(mcp_sdk_test_lifecycle
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_lifecycle_test COMMAND mcp_sdk_test_lifecycle)

add_executable(mcp_sdk_test_client client_test.cpp)

target_link_libraries(mcp_sdk_test_client
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_client_test COMMAND mcp_sdk_test_client)

add_executable(mcp_sdk_test_server server_test.cpp)

target_link_libraries(mcp_sdk_test_server
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_server_test COMMAND mcp_sdk_test_server)

add_executable(mcp_sdk_test_tasks tasks_test.cpp)

target_link_libraries(mcp_sdk_test_tasks
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_tasks_test COMMAND mcp_sdk_test_tasks)

add_executable(mcp_sdk_test_auth_protected_resource_metadata auth_protected_resource_metadata_test.cpp)

target_link_libraries(mcp_sdk_test_auth_protected_resource_metadata
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_auth_protected_resource_metadata_test COMMAND mcp_sdk_test_auth_protected_resource_metadata)

add_executable(mcp_sdk_test_transport_stdio transport_stdio_test.cpp)

target_link_libraries(mcp_sdk_test_transport_stdio
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_transport_stdio_test COMMAND mcp_sdk_test_transport_stdio)

add_executable(mcp_sdk_test_stdio_subprocess_server transport_stdio_subprocess_server.cpp)

target_link_libraries(mcp_sdk_test_stdio_subprocess_server
    PRIVATE
        mcp::sdk
)

target_compile_definitions(mcp_sdk_test_client
    PRIVATE
        MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH="$<TARGET_FILE:mcp_sdk_test_stdio_subprocess_server>"
)

add_executable(mcp_sdk_test_transport_stdio_subprocess transport_stdio_subprocess_test.cpp)

target_link_libraries(mcp_sdk_test_transport_stdio_subprocess
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

target_compile_definitions(mcp_sdk_test_transport_stdio_subprocess
    PRIVATE
        MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH="$<TARGET_FILE:mcp_sdk_test_stdio_subprocess_server>"
)

add_test(NAME mcp_sdk_transport_stdio_subprocess_test COMMAND mcp_sdk_test_transport_stdio_subprocess)

add_executable(mcp_sdk_test_transport_http_common transport_http_common_test.cpp)

target_link_libraries(mcp_sdk_test_transport_http_common
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_transport_http_common_test COMMAND mcp_sdk_test_transport_http_common)

add_executable(mcp_sdk_test_transport_http_common_integration transport_http_common_integration_test.cpp)

target_link_libraries(mcp_sdk_test_transport_http_common_integration
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_transport_http_common_integration_test COMMAND mcp_sdk_test_transport_http_common_integration)

add_executable(mcp_sdk_test_transport_http_server transport_http_server_test.cpp)

target_link_libraries(mcp_sdk_test_transport_http_server
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_transport_http_server_test COMMAND mcp_sdk_test_transport_http_server)

add_executable(mcp_sdk_test_transport_http_client transport_http_client_test.cpp)

target_link_libraries(mcp_sdk_test_transport_http_client
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_transport_http_client_test COMMAND mcp_sdk_test_transport_http_client)

add_executable(mcp_sdk_test_transport_http_tls transport_http_tls_test.cpp)

target_link_libraries(mcp_sdk_test_transport_http_tls
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

target_compile_definitions(mcp_sdk_test_transport_http_tls
    PRIVATE
        MCP_TEST_TLS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME mcp_sdk_transport_http_tls_test COMMAND mcp_sdk_test_transport_http_tls)
