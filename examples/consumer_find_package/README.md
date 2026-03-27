# Consumer `find_package()` Example

This example demonstrates how to consume the MCP C++ SDK in a standard CMake project after it has been installed globally or to a specific prefix on your system.

It uses the standard CMake package discovery mechanism:

```cmake
find_package(mcp_sdk CONFIG REQUIRED)
target_link_libraries(<your-target> PRIVATE mcp::sdk)
```

## How it works

When the SDK is installed (via `make install` or `cmake --install`), it exports CMake configuration files (`mcp_sdkConfig.cmake`). The `find_package` command locates these files to import the `mcp::sdk` target, which automatically sets up the necessary include directories and linker flags.

## Build

To test this example, you must first install the SDK to a local prefix directory, and then point the consumer build to that prefix. Run from the repository root:

```bash
# 1. Install the SDK to a local prefix directory
cmake --install build/vcpkg-unix-release --prefix build/prefix

# 2. Configure the consumer example, telling it where to find the installed package
cmake -S examples/consumer_find_package -B build-consumer -DCMAKE_PREFIX_PATH=build/prefix

# 3. Build the consumer example
cmake --build build-consumer
```