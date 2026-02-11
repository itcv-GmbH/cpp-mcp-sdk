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

add_test(
    NAME mcp_sdk_integration_setup_reference_python_sdk
    COMMAND
        "${Python3_EXECUTABLE}"
        "${MCP_SDK_INTEGRATION_PYTHON_SETUP_SCRIPT}"
        --venv
        "${MCP_SDK_INTEGRATION_PYTHON_VENV_DIR}"
        --requirements
        "${MCP_SDK_INTEGRATION_PYTHON_REQUIREMENTS}"
)

set_tests_properties(
    mcp_sdk_integration_setup_reference_python_sdk
    PROPERTIES
        LABELS "integration"
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
        DEPENDS mcp_sdk_integration_setup_reference_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration"
        TIMEOUT 300
)

add_test(
    NAME mcp_sdk_integration_cpp_client_to_reference_server
    COMMAND
        "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cpp_client_to_reference_server.py"
        --cpp-client
        "$<TARGET_FILE:mcp_sdk_test_integration_cpp_client>"
        --reference-server-script
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reference_python_server.py"
)

set_tests_properties(
    mcp_sdk_integration_cpp_client_to_reference_server
    PROPERTIES
        DEPENDS mcp_sdk_integration_setup_reference_python_sdk
        ENVIRONMENT "PYTHONUNBUFFERED=1"
        LABELS "integration"
        TIMEOUT 300
)
