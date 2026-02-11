cmake_minimum_required(VERSION 3.16)

project(mcp_sdk_consumer_vcpkg_overlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(mcp_sdk CONFIG REQUIRED)

add_executable(mcp_sdk_consumer_vcpkg_overlay
  main.cpp
)

target_link_libraries(mcp_sdk_consumer_vcpkg_overlay
  PRIVATE
    mcp::sdk
)
