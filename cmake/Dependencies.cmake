# Dependencies configuration for MCP SDK
# Uses vcpkg-provided packages only - no FetchContent

# Required dependencies

# JSON and JSON Schema 2020-12 support
# jsoncons is header-only and provides jsoncons target (vcpkg-generated usage)
find_package(jsoncons CONFIG REQUIRED)

# Boost libraries for networking, HTTP, and process management
# Note: vcpkg provides these as separate config packages, not Boost COMPONENTS
find_package(boost_asio CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)

# boost-process is not available on UWP platform
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    find_package(boost_process CONFIG REQUIRED)
endif()

# OpenSSL for TLS/HTTPS support
find_package(OpenSSL REQUIRED)

# Optional dependencies based on build options

if(MCP_SDK_BUILD_TESTS)
    # Catch2 testing framework
    # Provides: Catch2::Catch2 and Catch2::Catch2WithMain
    find_package(Catch2 CONFIG REQUIRED)
endif()

# Note: Examples do not require additional dependencies beyond the core SDK

# Display dependency summary
message(STATUS "MCP SDK Dependencies:")
message(STATUS "  jsoncons: ${jsoncons_VERSION} (JSON + JSON Schema 2020-12)")
message(STATUS "  Boost.Asio: ${boost_asio_VERSION}")
message(STATUS "  Boost.Beast: ${boost_beast_VERSION}")
if(TARGET Boost::process)
    message(STATUS "  Boost.Process: ${boost_process_VERSION}")
endif()
message(STATUS "  OpenSSL: ${OPENSSL_VERSION} (TLS/HTTPS)")
if(MCP_SDK_BUILD_TESTS)
    message(STATUS "  Catch2: ${Catch2_VERSION} (testing)")
endif()
