add_executable(mcp_sdk_example_http_client_auth
  main.cpp
)

target_link_libraries(mcp_sdk_example_http_client_auth
  PRIVATE
    mcp::sdk
)
