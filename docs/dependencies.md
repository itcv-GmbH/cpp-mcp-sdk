# Dependencies

This document describes the dependencies required to build and use the MCP C++ SDK.

## Required Dependencies

These dependencies are always required to build the SDK:

### JSON and JSON Schema

| Library | Vcpkg Port | CMake Target | Purpose |
|---------|------------|--------------|---------|
| jsoncons | `jsoncons` | `jsoncons` | JSON representation and JSON Schema 2020-12 validation (header-only) |

**Rationale**: jsoncons was selected because it explicitly supports JSON Schema draft 2020-12, which is required by the MCP specification. Other validators that only support draft-07 are insufficient.

### Networking and HTTP

| Library | Vcpkg Port | CMake Target | Purpose |
|---------|------------|--------------|---------|
| Boost.Asio | `boost-asio` | `Boost::asio` | Asynchronous I/O and networking |
| Boost.Beast | `boost-beast` | `Boost::beast` | HTTP/WebSocket protocols built on Asio |

### Process Management

| Library | Vcpkg Port | CMake Target | Purpose |
|---------|------------|--------------|---------|
| Boost.Process | `boost-process` | `Boost::process` | Subprocess management for stdio transport |

**Note**: Boost.Process is not supported on UWP (Universal Windows Platform). The stdio transport will not be available on that platform.

### TLS/SSL

| Library | Vcpkg Port | CMake Target | Purpose |
|---------|------------|--------------|---------|
| OpenSSL | `openssl` | `OpenSSL::SSL`, `OpenSSL::Crypto` | HTTPS/TLS support for client and server |

## Optional Dependencies

### Testing Framework

| Library | Vcpkg Port | CMake Target | Purpose |
|---------|------------|--------------|---------|
| Catch2 | `catch2` | `Catch2::Catch2`, `Catch2::Catch2WithMain` | Unit and conformance testing |

**When Required**: Catch2 is always installed as a default dependency since `MCP_SDK_BUILD_TESTS` defaults to ON. Tests are built by default; disable with `-DMCP_SDK_BUILD_TESTS=OFF` to skip test compilation.

## Dependency Installation

### Using vcpkg Manifest Mode (Recommended)

The SDK uses vcpkg manifest mode. Dependencies are automatically installed when you configure the project:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

To install with tests enabled via vcpkg features:

```bash
vcpkg install --feature-flags=manifests --x-feature=tests
```

### Platform Notes

- **Desktop OSes** (Linux, macOS, Windows): All dependencies are fully supported
- **UWP/Windows Store**: Boost.Process is not available; stdio transport will be disabled
- **C++17**: All dependencies are compatible with C++17 baseline

## Version Pinning

The SDK uses explicit version pinning via:

1. **`builtin-baseline`** in `vcpkg.json`: Pins the vcpkg registry commit
2. **Minimum versions** in `vcpkg.json` dependencies: Ensures required features are available

The `vcpkg-configuration.json` file is reserved for overlay ports (local custom ports) and does not override the default registry. Upgrading dependencies requires updating the `builtin-baseline` in `vcpkg.json`.

## CMake Integration

Dependencies are located using standard CMake `find_package()`:

```cmake
# Required dependencies
find_package(jsoncons CONFIG REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)
find_package(boost_process CONFIG REQUIRED)  # Not available on UWP
find_package(OpenSSL REQUIRED)

# Optional test dependency (only when tests enabled)
if(MCP_SDK_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
endif()
```

All dependencies are linked via imported CMake targets (no FetchContent).

## Future Dependencies

The following may be added in future versions:

- **fmt**: For formatting (since `std::format` is not available in C++17)
- **spdlog**: For structured logging (optional feature)
- **zlib**: For compression support in HTTP transport
