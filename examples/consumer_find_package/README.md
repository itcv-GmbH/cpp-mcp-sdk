# Consumer `find_package()` Example

This example demonstrates consuming an installed SDK package using:

```cmake
find_package(mcp_sdk CONFIG REQUIRED)
target_link_libraries(<your-target> PRIVATE mcp::sdk)
```

## Build

From the repository root after installing the SDK to `build/prefix`:

```bash
cmake -S examples/consumer_find_package -B build-consumer -DCMAKE_PREFIX_PATH=build/prefix
cmake --build build-consumer
```
