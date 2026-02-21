add_executable(mcp_sdk_example_http_listen
  main.cpp
)

target_link_libraries(mcp_sdk_example_http_listen
  PRIVATE
    mcp::sdk
)
