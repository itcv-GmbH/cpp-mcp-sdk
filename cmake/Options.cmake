# CMake Options for MCP SDK

# Build shared or static library
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Feature options
option(MCP_SDK_BUILD_TESTS "Build tests" ON)
option(MCP_SDK_BUILD_EXAMPLES "Build examples" ON)
option(MCP_SDK_ENABLE_TLS "Enable TLS support" ON)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Multi-config generator safety: ensure we handle configuration types correctly
if(CMAKE_CONFIGURATION_TYPES)
    # Multi-config generator (Visual Studio, Xcode, Ninja Multi-Config)
    message(STATUS "Multi-config generator detected: ${CMAKE_CONFIGURATION_TYPES}")
else()
    # Single-config generator (Unix Makefiles, Ninja)
    message(STATUS "Single-config generator detected, build type: ${CMAKE_BUILD_TYPE}")
endif()
