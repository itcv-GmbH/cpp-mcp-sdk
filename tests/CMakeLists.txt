# Tests CMakeLists.txt

# Exclude unit test targets from clang-tidy; keep analysis focused on SDK sources.
set(CMAKE_CXX_CLANG_TIDY "")

add_executable(mcp_sdk_test_smoke smoke_test.cpp)

target_link_libraries(mcp_sdk_test_smoke
    PRIVATE
        mcp::sdk
)

add_executable(mcp_sdk_test_pinned_mirror conformance/test_pinned_mirror.cpp)

target_link_libraries(mcp_sdk_test_pinned_mirror
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_jsonrpc_invariants conformance/test_jsonrpc_invariants.cpp)

target_link_libraries(mcp_sdk_test_conformance_jsonrpc_invariants
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_lifecycle conformance/test_lifecycle.cpp)

target_link_libraries(mcp_sdk_test_conformance_lifecycle
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_capabilities conformance/test_capabilities.cpp)

target_link_libraries(mcp_sdk_test_conformance_capabilities
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_stdio_transport conformance/test_stdio_transport.cpp)

target_link_libraries(mcp_sdk_test_conformance_stdio_transport
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_streamable_http_transport conformance/test_streamable_http_transport.cpp)

target_link_libraries(mcp_sdk_test_conformance_streamable_http_transport
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_legacy_client conformance/test_legacy_client.cpp)

target_link_libraries(mcp_sdk_test_conformance_legacy_client
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_legacy_server conformance/test_legacy_server.cpp)

target_link_libraries(mcp_sdk_test_conformance_legacy_server
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_tasks conformance/test_tasks.cpp)

target_link_libraries(mcp_sdk_test_conformance_tasks
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_executable(mcp_sdk_test_conformance_authorization conformance/test_authorization.cpp)

target_link_libraries(mcp_sdk_test_conformance_authorization
    PRIVATE
        Catch2::Catch2WithMain
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
add_test(NAME mcp_sdk_pinned_mirror_test COMMAND mcp_sdk_test_pinned_mirror)
add_test(NAME mcp_sdk_conformance_pinned_mirror_test COMMAND mcp_sdk_test_pinned_mirror)
add_test(NAME mcp_sdk_conformance_jsonrpc_invariants_test COMMAND mcp_sdk_test_conformance_jsonrpc_invariants)
add_test(NAME mcp_sdk_conformance_lifecycle_test COMMAND mcp_sdk_test_conformance_lifecycle)
add_test(NAME mcp_sdk_conformance_capabilities_test COMMAND mcp_sdk_test_conformance_capabilities)
add_test(NAME mcp_sdk_conformance_stdio_transport_test COMMAND mcp_sdk_test_conformance_stdio_transport)
add_test(NAME mcp_sdk_conformance_streamable_http_transport_test COMMAND mcp_sdk_test_conformance_streamable_http_transport)
add_test(NAME mcp_sdk_conformance_legacy_client_test COMMAND mcp_sdk_test_conformance_legacy_client)
add_test(NAME mcp_sdk_conformance_legacy_server_test COMMAND mcp_sdk_test_conformance_legacy_server)
add_test(NAME mcp_sdk_conformance_tasks_test COMMAND mcp_sdk_test_conformance_tasks)
add_test(NAME mcp_sdk_conformance_authorization_test_authorization COMMAND mcp_sdk_test_conformance_authorization)
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

add_executable(mcp_sdk_test_detail_initialize_codec detail_initialize_codec_test.cpp)

target_link_libraries(mcp_sdk_test_detail_initialize_codec
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_detail_initialize_codec_test COMMAND mcp_sdk_test_detail_initialize_codec)

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

add_executable(mcp_sdk_test_runtime_limits runtime_limits_test.cpp)

target_link_libraries(mcp_sdk_test_runtime_limits
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_runtime_limits_test COMMAND mcp_sdk_test_runtime_limits)

add_executable(mcp_sdk_test_auth_protected_resource_metadata auth_protected_resource_metadata_test.cpp)

target_link_libraries(mcp_sdk_test_auth_protected_resource_metadata
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_auth_protected_resource_metadata_test_authorization COMMAND mcp_sdk_test_auth_protected_resource_metadata)

add_executable(mcp_sdk_test_auth_client_registration auth_client_registration_test.cpp)

target_link_libraries(mcp_sdk_test_auth_client_registration
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_auth_client_registration_test_authorization COMMAND mcp_sdk_test_auth_client_registration)

add_executable(mcp_sdk_test_auth_oauth_client auth_oauth_client_test.cpp)

target_link_libraries(mcp_sdk_test_auth_oauth_client
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_auth_oauth_client_test_authorization COMMAND mcp_sdk_test_auth_oauth_client)

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

# New unit test executables (scaffolded for future tasks)
add_executable(mcp_sdk_test_util_cancellation util_cancellation_test.cpp)

target_link_libraries(mcp_sdk_test_util_cancellation
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_util_cancellation_test COMMAND mcp_sdk_test_util_cancellation)

add_executable(mcp_sdk_test_util_progress util_progress_test.cpp)

target_link_libraries(mcp_sdk_test_util_progress
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_util_progress_test COMMAND mcp_sdk_test_util_progress)

add_executable(mcp_sdk_test_schema_pinned_schema schema_pinned_schema_test.cpp)

target_link_libraries(mcp_sdk_test_schema_pinned_schema
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_schema_pinned_schema_test COMMAND mcp_sdk_test_schema_pinned_schema)

add_executable(mcp_sdk_test_sdk_version sdk_version_test.cpp)

target_link_libraries(mcp_sdk_test_sdk_version
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_sdk_version_test COMMAND mcp_sdk_test_sdk_version)

add_executable(mcp_sdk_test_security_crypto_random security_crypto_random_test.cpp)

target_link_libraries(mcp_sdk_test_security_crypto_random
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_security_crypto_random_test COMMAND mcp_sdk_test_security_crypto_random)

add_executable(mcp_sdk_test_security_limits security_limits_test.cpp)

target_link_libraries(mcp_sdk_test_security_limits
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_security_limits_test COMMAND mcp_sdk_test_security_limits)

add_executable(mcp_sdk_test_transport_http_runtime transport_http_runtime_test.cpp)

target_link_libraries(mcp_sdk_test_transport_http_runtime
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_transport_http_runtime_test COMMAND mcp_sdk_test_transport_http_runtime)

add_executable(mcp_sdk_test_auth_oauth_client_disabled auth_oauth_client_disabled_test.cpp)

target_link_libraries(mcp_sdk_test_auth_oauth_client_disabled
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_auth_oauth_client_disabled_test COMMAND mcp_sdk_test_auth_oauth_client_disabled)

add_executable(mcp_sdk_test_detail_ascii detail_ascii_test.cpp)

target_link_libraries(mcp_sdk_test_detail_ascii
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_detail_ascii_test COMMAND mcp_sdk_test_detail_ascii)

add_executable(mcp_sdk_test_detail_base64url detail_base64url_test.cpp)

target_link_libraries(mcp_sdk_test_detail_base64url
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_detail_base64url_test COMMAND mcp_sdk_test_detail_base64url)

add_executable(mcp_sdk_test_detail_url detail_url_test.cpp)

target_link_libraries(mcp_sdk_test_detail_url
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_detail_url_test COMMAND mcp_sdk_test_detail_url)

add_executable(mcp_sdk_test_server_stdio_runner server_stdio_runner_test.cpp)

target_link_libraries(mcp_sdk_test_server_stdio_runner
    PRIVATE
        Catch2::Catch2WithMain
        mcp::sdk
)

add_test(NAME mcp_sdk_server_stdio_runner_test COMMAND mcp_sdk_test_server_stdio_runner)

if(MCP_SDK_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
