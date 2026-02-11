cmake_minimum_required(VERSION 3.16)

project(mcp_sdk_consumer_find_package LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/../../build/prefix")

file(GLOB MCP_SDK_VCPKG_INSTALLED_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/../../build/vcpkg-unix-release/vcpkg_installed/*")
foreach(MCP_SDK_VCPKG_TRIPLET_DIR IN LISTS MCP_SDK_VCPKG_INSTALLED_TRIPLETS)
  if(IS_DIRECTORY "${MCP_SDK_VCPKG_TRIPLET_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${MCP_SDK_VCPKG_TRIPLET_DIR}")
  endif()
endforeach()

find_package(mcp_sdk CONFIG REQUIRED)

add_executable(mcp_sdk_consumer
  main.cpp
)

target_link_libraries(mcp_sdk_consumer
  PRIVATE
    mcp::sdk
)
