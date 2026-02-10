# Examples CMakeLists.txt

add_executable(mcp_sdk_example_minimal minimal_example.cpp)

target_link_libraries(mcp_sdk_example_minimal
    PRIVATE
        mcp::sdk
)
