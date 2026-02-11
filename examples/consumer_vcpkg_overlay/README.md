# Consumer Overlay Port Verification

This project verifies that the SDK can be consumed from the local vcpkg overlay port with `find_package(mcp_sdk CONFIG REQUIRED)`.

Run from the repository root:

```bash
vcpkg install mcp-cpp-sdk --overlay-ports=./vcpkg/ports --classic
cmake -S examples/consumer_vcpkg_overlay -B build-consumer -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
cmake --build build-consumer
```
