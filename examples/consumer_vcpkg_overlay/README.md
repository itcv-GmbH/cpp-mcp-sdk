# Consumer Overlay Port Verification

This example demonstrates how to consume the MCP C++ SDK using a **vcpkg overlay port**.

An overlay port allows you to use the SDK as a standard vcpkg dependency (`vcpkg install mcp-cpp-sdk`) locally without it needing to be merged into the official Microsoft vcpkg registry yet.

This project verifies that the SDK can be correctly resolved and linked from the local vcpkg overlay port via CMake's `find_package(mcp_sdk CONFIG REQUIRED)`.

## How it works

The `CMakeLists.txt` simply searches for the package and links against it:
```cmake
find_package(mcp_sdk CONFIG REQUIRED)
target_link_libraries(mcp_sdk_example_consumer_vcpkg_overlay PRIVATE mcp::sdk)
```

## Build

Run these commands from the repository root to test the overlay installation:

```bash
# 1. Install the SDK using the local overlay port directory
vcpkg install mcp-cpp-sdk --overlay-ports=./vcpkg/ports --classic

# 2. Configure the example project to use the vcpkg toolchain
cmake -S examples/consumer_vcpkg_overlay -B build-consumer -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"

# 3. Build the example
cmake --build build-consumer
```