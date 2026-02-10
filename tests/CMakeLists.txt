# Tests CMakeLists.txt

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
