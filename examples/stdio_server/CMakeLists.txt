add_executable(mcp_sdk_example_stdio_server
  main.cpp
)

target_link_libraries(mcp_sdk_example_stdio_server
  PRIVATE
    mcp::sdk
)
