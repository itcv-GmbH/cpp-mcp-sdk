# Examples CMakeLists.txt

add_executable(mcp_sdk_example_minimal minimal_example.cpp)

target_link_libraries(mcp_sdk_example_minimal
    PRIVATE
        mcp::sdk
)

# On Windows with shared libraries, copy DLL to example output directory
if(WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(TARGET mcp_sdk_example_minimal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mcp_sdk>
            $<TARGET_FILE_DIR:mcp_sdk_example_minimal>
        COMMENT "Copying mcp_sdk.dll to example output directory"
    )
endif()

add_subdirectory(stdio_server)
add_subdirectory(bidirectional_sampling_elicitation)
add_subdirectory(dual_transport_server)
add_subdirectory(http_listen_example)

if(MCP_SDK_ENABLE_TLS AND MCP_SDK_ENABLE_AUTH)
    add_subdirectory(http_server_auth)
    add_subdirectory(http_client_auth)
endif()
