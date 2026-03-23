# Tests CMakeLists.txt

# Exclude unit test targets from clang-tidy; keep analysis focused on SDK sources.
set(CMAKE_CXX_CLANG_TIDY "")

# Generate test TLS certificates before running tests
find_program(OPENSSL_EXECUTABLE openssl REQUIRED)
set(TEST_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(TEST_CERTS_GENERATED "${TEST_FIXTURES_DIR}/tls-localhost-key.pem")

add_custom_command(
    OUTPUT ${TEST_CERTS_GENERATED}
    COMMAND ${CMAKE_COMMAND} -E echo "Generating test TLS certificates..."
    COMMAND bash ${TEST_FIXTURES_DIR}/generate_test_certs.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating test TLS certificates for TLS tests"
    VERBATIM
)

add_custom_target(generate_test_certs ALL DEPENDS ${TEST_CERTS_GENERATED})

# Helper function to define a test executable
function(mcp_add_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Catch2::Catch2WithMain
            mcp::sdk
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Helper function for tests without Catch2 (e.g., smoke tests)
function(mcp_add_test_no_catch2 TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            mcp::sdk
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

mcp_add_test_no_catch2(mcp_sdk_test_smoke smoke_test.cpp)
mcp_add_test(mcp_sdk_test_pinned_mirror conformance/test_pinned_mirror.cpp)

mcp_add_test(mcp_sdk_test_conformance_jsonrpc_invariants conformance/test_jsonrpc_invariants.cpp)
mcp_add_test(mcp_sdk_test_conformance_lifecycle conformance/test_lifecycle.cpp)
mcp_add_test(mcp_sdk_test_conformance_capabilities conformance/test_capabilities.cpp)
mcp_add_test(mcp_sdk_test_conformance_stdio_transport conformance/test_stdio_transport.cpp)
mcp_add_test(mcp_sdk_test_conformance_streamable_http_transport conformance/test_streamable_http_transport.cpp)
mcp_add_test(mcp_sdk_test_conformance_legacy_client conformance/test_legacy_client.cpp)
mcp_add_test(mcp_sdk_test_conformance_legacy_server conformance/test_legacy_server.cpp)
mcp_add_test(mcp_sdk_test_conformance_tasks conformance/test_tasks.cpp)
mcp_add_test(mcp_sdk_test_conformance_authorization conformance/test_authorization.cpp)

mcp_add_test(mcp_sdk_test_jsonrpc_messages jsonrpc_messages_test.cpp)
mcp_add_test(mcp_sdk_test_jsonrpc_router jsonrpc_router_test.cpp)
mcp_add_test(mcp_sdk_test_jsonrpc_router_concurrency jsonrpc/router_concurrency_test.cpp)
mcp_add_test(mcp_sdk_test_schema_validator schema_validator_test.cpp)
mcp_add_test(mcp_sdk_test_lifecycle lifecycle_test.cpp)
mcp_add_test(mcp_sdk_test_detail_initialize_codec detail_initialize_codec_test.cpp)
mcp_add_test(mcp_sdk_test_detail_thread_boundary detail/thread_boundary_test.cpp)
mcp_add_test(mcp_sdk_test_client client_test.cpp)
mcp_add_test(mcp_sdk_test_client_http_listen client_http_listen_test.cpp)
mcp_add_test(mcp_sdk_test_server server_test.cpp)
mcp_add_test(mcp_sdk_test_tasks tasks_test.cpp)
mcp_add_test(mcp_sdk_test_runtime_limits runtime_limits_test.cpp)
mcp_add_test(mcp_sdk_test_auth_protected_resource_metadata auth_protected_resource_metadata_test.cpp)
mcp_add_test(mcp_sdk_test_auth_client_registration auth_client_registration_test.cpp)
mcp_add_test(mcp_sdk_test_auth_oauth_client auth_oauth_client_test.cpp)
mcp_add_test(mcp_sdk_test_transport_stdio transport_stdio_test.cpp)

mcp_add_test_no_catch2(mcp_sdk_test_stdio_subprocess_server transport_stdio_subprocess_server.cpp)

target_compile_definitions(mcp_sdk_test_client
    PRIVATE
        MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH="$<TARGET_FILE:mcp_sdk_test_stdio_subprocess_server>"
)

mcp_add_test(mcp_sdk_test_transport_stdio_subprocess transport_stdio_subprocess_test.cpp)

target_compile_definitions(mcp_sdk_test_transport_stdio_subprocess
    PRIVATE
        MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH="$<TARGET_FILE:mcp_sdk_test_stdio_subprocess_server>"
)

mcp_add_test(mcp_sdk_test_transport_http_common transport_http_common_test.cpp)
mcp_add_test(mcp_sdk_test_transport_http_common_integration transport_http_common_integration_test.cpp)
mcp_add_test(mcp_sdk_test_transport_http_server transport_http_server_test.cpp)
mcp_add_test(mcp_sdk_test_transport_http_client transport_http_client_test.cpp)

mcp_add_test(mcp_sdk_test_transport_http_tls transport_http_tls_test.cpp)
target_compile_definitions(mcp_sdk_test_transport_http_tls
    PRIVATE
        MCP_TEST_TLS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

mcp_add_test(mcp_sdk_test_util_cancellation util_cancellation_test.cpp)
mcp_add_test(mcp_sdk_test_util_progress util_progress_test.cpp)
mcp_add_test(mcp_sdk_test_schema_pinned_schema schema_pinned_schema_test.cpp)
mcp_add_test(mcp_sdk_test_sdk_version sdk_version_test.cpp)
mcp_add_test(mcp_sdk_test_security_crypto_random security_crypto_random_test.cpp)
mcp_add_test(mcp_sdk_test_security_limits security_limits_test.cpp)
mcp_add_test(mcp_sdk_test_transport_http_runtime transport_http_runtime_test.cpp)
mcp_add_test(mcp_sdk_test_auth_oauth_client_disabled auth_oauth_client_disabled_test.cpp)
mcp_add_test(mcp_sdk_test_detail_ascii detail_ascii_test.cpp)
mcp_add_test(mcp_sdk_test_detail_base64url detail_base64url_test.cpp)
mcp_add_test(mcp_sdk_test_detail_url detail_url_test.cpp)
mcp_add_test(mcp_sdk_test_server_stdio_runner server_stdio_runner_test.cpp)
mcp_add_test(mcp_sdk_test_server_streamable_http_runner server_streamable_http_runner_test.cpp)
mcp_add_test(mcp_sdk_test_server_combined_runner server_combined_runner_test.cpp)
mcp_add_test(mcp_sdk_test_transport_threading transport_threading_test.cpp)
mcp_add_test(mcp_sdk_test_concurrency_concurrent_routing concurrency/concurrent_routing_test.cpp)
mcp_add_test(mcp_sdk_test_concurrency_exception_containment concurrency/exception_containment_test.cpp)

mcp_add_test(mcp_sdk_test_concurrency_idempotent_lifecycle concurrency/idempotent_lifecycle_test.cpp)
target_compile_definitions(mcp_sdk_test_concurrency_idempotent_lifecycle
    PRIVATE
        MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH="$<TARGET_FILE:mcp_sdk_test_stdio_subprocess_server>"
)

mcp_add_test(mcp_sdk_test_concurrency_destructor_safety concurrency/destructor_safety_test.cpp)

if(MCP_SDK_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
