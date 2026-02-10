# Tests CMakeLists.txt

add_executable(mcp_sdk_test_smoke smoke_test.cpp)

target_link_libraries(mcp_sdk_test_smoke
    PRIVATE
        mcp::sdk
)

# Register with CTest
add_test(NAME mcp_sdk_smoke_test COMMAND mcp_sdk_test_smoke)
