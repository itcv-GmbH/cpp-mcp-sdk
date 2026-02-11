add_executable(mcp_sdk_example_bidirectional_sampling_elicitation
  main.cpp
)

target_link_libraries(mcp_sdk_example_bidirectional_sampling_elicitation
  PRIVATE
    mcp::sdk
)
