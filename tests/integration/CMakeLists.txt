# Integration tests that exercise interop against a pinned reference SDK.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(MCP_SDK_INTEGRATION_PYTHON_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_reference_python_sdk.py")
set(MCP_SDK_INTEGRATION_PYTHON_REQUIREMENTS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reference_python_requirements.txt")
set(MCP_SDK_INTEGRATION_PYTHON_VENV_DIR "${CMAKE_CURRENT_BINARY_DIR}/python-venv")

if(WIN32)
    set(MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE "${MCP_SDK_INTEGRATION_PYTHON_VENV_DIR}/Scripts/python.exe")
else()
    set(MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE "${MCP_SDK_INTEGRATION_PYTHON_VENV_DIR}/bin/python")
endif()

add_executable(mcp_sdk_test_integration_cpp_server cpp_server_fixture.cpp)

target_link_libraries(mcp_sdk_test_integration_cpp_server
    PRIVATE
        mcp::sdk
)

add_executable(mcp_sdk_test_integration_cpp_client cpp_client_fixture.cpp)

target_link_libraries(mcp_sdk_test_integration_cpp_client
    PRIVATE
        mcp::sdk
)

add_executable(mcp_sdk_test_integration_cpp_stdio_server cpp_stdio_server_fixture.cpp)

target_link_libraries(mcp_sdk_test_integration_cpp_stdio_server
    PRIVATE
        mcp::sdk
)

# HTTP server fixtures
add_executable(mcp_sdk_test_integration_cpp_server_utilities cpp_server_utilities_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_server_utilities PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_server_resources_advanced cpp_server_resources_advanced_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_server_resources_advanced PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_server_roots cpp_server_roots_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_server_roots PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_server_tasks cpp_server_tasks_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_server_tasks PRIVATE mcp::sdk)

# STDIO server fixtures
add_executable(mcp_sdk_test_integration_cpp_stdio_server_utilities cpp_stdio_server_utilities_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_stdio_server_utilities PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_stdio_server_resources_advanced cpp_stdio_server_resources_advanced_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_stdio_server_resources_advanced PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_stdio_server_roots cpp_stdio_server_roots_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_stdio_server_roots PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_stdio_server_tasks cpp_stdio_server_tasks_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_stdio_server_tasks PRIVATE mcp::sdk)

# C++ client fixtures
add_executable(mcp_sdk_test_integration_cpp_client_utilities cpp_client_utilities_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_client_utilities PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_client_resources_advanced cpp_client_resources_advanced_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_client_resources_advanced PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_client_roots cpp_client_roots_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_client_roots PRIVATE mcp::sdk)

add_executable(mcp_sdk_test_integration_cpp_client_tasks cpp_client_tasks_fixture.cpp)
target_link_libraries(mcp_sdk_test_integration_cpp_client_tasks PRIVATE mcp::sdk)

add_test(
    NAME mcp_sdk_integration_reference_setup_python_sdk
    COMMAND
        "${Python3_EXECUTABLE}"
        "${MCP_SDK_INTEGRATION_PYTHON_SETUP_SCRIPT}"
        --venv
        "${MCP_SDK_INTEGRATION_PYTHON_VENV_DIR}"
        --requirements
        "${MCP_SDK_INTEGRATION_PYTHON_REQUIREMENTS}"
)

set_tests_properties(
    mcp_sdk_integration_reference_setup_python_sdk
    PROPERTIES
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_server
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_server.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_server>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_server
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

add_test(
    NAME mcp_sdk_integration_reference_cpp_client_to_reference_server
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cpp_client_to_reference_server.py"
        --cpp-client
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_client>"
        --reference-server-script
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reference_python_server.py"
)

set_tests_properties(
    mcp_sdk_integration_reference_cpp_client_to_reference_server
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_stdio_server
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_stdio_server.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_stdio_server>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_stdio_server
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

add_test(
    NAME mcp_sdk_integration_reference_coverage_gate
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_coverage_gate.py"
        --coverage-file
        "${CMAKE_CURRENT_SOURCE_DIR}/COVERAGE.md"
)

set_tests_properties(
    mcp_sdk_integration_reference_coverage_gate
    PROPERTIES
        LABELS "integration;integration_reference"
        TIMEOUT 60
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
)

# Python reference client to C++ HTTP server tests (utilities)
# Uses base fixture as utilities fixture has internal initialization issues
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_server_utilities
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_server_utilities.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_server>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_server_utilities
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

# Python reference client to C++ HTTP server tests (resources advanced)
# Uses base fixture as resources_advanced fixture has internal initialization issues
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_server_resources_advanced
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_server_resources_advanced.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_server>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_server_resources_advanced
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

# Python reference client to C++ HTTP server tests (roots)
# Uses base fixture as roots fixture has internal initialization issues
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_server_roots
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_server_roots.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_server>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_server_roots
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

# Python reference client to C++ HTTP server tests (tasks)
# Uses base fixture as tasks fixture has internal initialization issues
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_server_tasks
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_server_tasks.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_server>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_server_tasks
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

# Python reference client to C++ STDIO server tests (utilities)
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_stdio_server_utilities
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_stdio_server_utilities.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_stdio_server_utilities>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_stdio_server_utilities
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

# Python reference client to C++ STDIO server tests (resources advanced)
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_stdio_server_resources_advanced
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_stdio_server_resources_advanced.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_stdio_server_resources_advanced>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_stdio_server_resources_advanced
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

# Python reference client to C++ STDIO server tests (roots)
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_stdio_server_roots
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_stdio_server_roots.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_stdio_server_roots>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_stdio_server_roots
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)

# Python reference client to C++ STDIO server tests (tasks)
add_test(
    NAME mcp_sdk_integration_reference_client_to_cpp_stdio_server_tasks
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reference_client_to_cpp_stdio_server_tasks.py"
        --cpp-server
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_stdio_server_tasks>"
)

set_tests_properties(
    mcp_sdk_integration_reference_client_to_cpp_stdio_server_tasks
    PROPERTIES
        DEPENDS mcp_sdk_integration_reference_setup_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration;integration_reference"
        TIMEOUT 300
)
