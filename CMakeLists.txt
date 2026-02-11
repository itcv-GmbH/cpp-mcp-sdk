cmake_minimum_required(VERSION 3.16)

project(mcp_cpp_sdk
    VERSION 0.1.0
    DESCRIPTION "Model Context Protocol SDK for C++"
    LANGUAGES CXX
)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include options before anything else
include(cmake/Options.cmake)

# Set default build type if not specified (single-config generators)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Include dependencies configuration
include(cmake/Dependencies.cmake)

# Enable clang-tidy during build if available
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
else()
    message(WARNING "clang-tidy not found - static analysis disabled")
endif()

# Create the main SDK library
add_library(mcp_sdk)
add_library(mcp::sdk ALIAS mcp_sdk)

target_sources(mcp_sdk
    PRIVATE
        src/jsonrpc/messages.cpp
        src/jsonrpc/router.cpp
        src/lifecycle/session.cpp
        src/server/server.cpp
        src/schema/validator.cpp
        src/transport/http_client.cpp
        src/transport/http_runtime.cpp
        src/transport/http_server.cpp
        src/transport/stdio.cpp
        src/version.cpp
)

set(MCP_SDK_PINNED_MCP_SCHEMA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.docs/requirements/mcp-spec-2025-11-25/schema/schema.json")
if(NOT EXISTS "${MCP_SDK_PINNED_MCP_SCHEMA_FILE}")
    message(FATAL_ERROR "Pinned MCP schema file not found at: ${MCP_SDK_PINNED_MCP_SCHEMA_FILE}")
endif()

file(READ "${MCP_SDK_PINNED_MCP_SCHEMA_FILE}" MCP_SDK_PINNED_MCP_SCHEMA_JSON)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/schema/pinned_schema_data.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/src/schema/pinned_schema_data.cpp"
    @ONLY
)

target_sources(mcp_sdk
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/src/schema/pinned_schema_data.cpp"
)

target_link_libraries(mcp_sdk
    PUBLIC
        Boost::asio
        OpenSSL::Crypto
        OpenSSL::SSL
        jsoncons
)

if(TARGET Boost::process)
    target_link_libraries(mcp_sdk PUBLIC Boost::process)
endif()

target_include_directories(mcp_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties for shared/static
set_target_properties(mcp_sdk PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME mcp_sdk
    EXPORT_NAME sdk
)

# Build tests if enabled
if(MCP_SDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Build examples if enabled
if(MCP_SDK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install/export configuration
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Install the library
install(TARGETS mcp_sdk
    EXPORT mcp_sdk-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install the export targets
install(EXPORT mcp_sdk-targets
    FILE mcp_sdk-targets.cmake
    NAMESPACE mcp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcp_sdk
)

# Create package config file
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mcp_sdk-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mcp_sdk-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcp_sdk
)

# Create version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mcp_sdk-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install the package configuration files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mcp_sdk-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mcp_sdk-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcp_sdk
)

# Code quality targets
find_program(CLANG_FORMAT_EXE NAMES clang-format)

# Get all source files for formatting
file(GLOB_RECURSE ALL_SOURCE_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.hpp
)

# Filter out vcpkg_installed directory
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/vcpkg_installed/.*")

if(CLANG_FORMAT_EXE)
    add_custom_target(clang-format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running clang-format on source files"
        VERBATIM
    )
    
    add_custom_target(clang-format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking code formatting with clang-format"
        VERBATIM
    )
endif()
