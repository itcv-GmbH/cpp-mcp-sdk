cmake_minimum_required(VERSION 3.16)

project(mcp_sdk_consumer_find_package LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_mcp_sdk_normalized_prefix_path "")
get_filename_component(_mcp_sdk_repo_root "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
foreach(_mcp_sdk_prefix IN LISTS CMAKE_PREFIX_PATH)
  if(_mcp_sdk_prefix STREQUAL "")
    continue()
  endif()

  if(IS_ABSOLUTE "${_mcp_sdk_prefix}")
    list(APPEND _mcp_sdk_normalized_prefix_path "${_mcp_sdk_prefix}")
  else()
    get_filename_component(_mcp_sdk_prefix_from_source "${_mcp_sdk_prefix}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    get_filename_component(_mcp_sdk_prefix_from_repo_root "${_mcp_sdk_prefix}" ABSOLUTE BASE_DIR "${_mcp_sdk_repo_root}")

    if(EXISTS "${_mcp_sdk_prefix_from_source}")
      list(APPEND _mcp_sdk_normalized_prefix_path "${_mcp_sdk_prefix_from_source}")
    elseif(EXISTS "${_mcp_sdk_prefix_from_repo_root}")
      list(APPEND _mcp_sdk_normalized_prefix_path "${_mcp_sdk_prefix_from_repo_root}")
    else()
      list(APPEND _mcp_sdk_normalized_prefix_path "${_mcp_sdk_prefix_from_source}")
    endif()
  endif()
endforeach()
set(CMAKE_PREFIX_PATH "${_mcp_sdk_normalized_prefix_path}")

find_package(mcp_sdk CONFIG REQUIRED)

add_executable(mcp_sdk_consumer
  main.cpp
)

target_link_libraries(mcp_sdk_consumer
  PRIVATE
    mcp::sdk
)
