add_executable(mcp_sdk_example_http_server_auth
  main.cpp
)

target_link_libraries(mcp_sdk_example_http_server_auth
  PRIVATE
    mcp::sdk
)
