add_executable(mcp_sdk_example_dual_transport_server
  main.cpp
)

target_link_libraries(mcp_sdk_example_dual_transport_server
  PRIVATE
    mcp::sdk
)
