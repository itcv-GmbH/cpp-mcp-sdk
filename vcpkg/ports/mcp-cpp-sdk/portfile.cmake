if(NOT DEFINED SOURCE_PATH)
    get_filename_component(SOURCE_PATH "${CURRENT_PORT_DIR}/../../.." ABSOLUTE)
endif()

if(NOT EXISTS "${SOURCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "mcp-cpp-sdk overlay port could not locate source tree at: ${SOURCE_PATH}")
endif()

set(MCP_SDK_BUILD_SHARED OFF)
if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(MCP_SDK_BUILD_SHARED ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=${MCP_SDK_BUILD_SHARED}
        -DMCP_SDK_BUILD_TESTS=OFF
        -DMCP_SDK_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME mcp_sdk
    CONFIG_PATH lib/cmake/mcp_sdk
)

if(EXISTS "${CURRENT_PACKAGES_DIR}/share/mcp_sdk")
    file(COPY "${CURRENT_PACKAGES_DIR}/share/mcp_sdk/" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright"
"mcp-cpp-sdk\n\n"
"SPDX-License-Identifier: MIT\n\n"
"Local overlay port for mcp-cpp-sdk development verification.\n")
