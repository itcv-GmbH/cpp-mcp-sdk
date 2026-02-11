# Examples CMakeLists.txt

add_executable(mcp_sdk_example_minimal minimal_example.cpp)

target_link_libraries(mcp_sdk_example_minimal
    PRIVATE
        mcp::sdk
)

add_subdirectory(stdio_server)
add_subdirectory(http_server_auth)
add_subdirectory(http_client_auth)
add_subdirectory(bidirectional_sampling_elicitation)
