# AGENTS.md - MCP C++ SDK Development Guide

Model Context Protocol (MCP) SDK for C++ - A CMake-based C++20 library for implementing MCP servers and clients.

## Build Commands

### Quick Start (any platform)

```bash
cmake --preset vcpkg-unix-release            # macOS/Linux
cmake --preset vcpkg-windows-release          # Windows
cmake --build build/vcpkg-unix-release        # macOS/Linux
cmake --build build/vcpkg-windows-release --config Release --parallel  # Windows
```

### Presets

| Platform | First build / CI | Cached deps |
|----------|-------------------|-------------|
| macOS/Linux | `vcpkg-unix-release` | `unix-release` |
| Windows | `vcpkg-windows-release` | `windows-release` |

Debug variants: replace `-release` with `-debug`.

Windows presets use Visual Studio 17 2022 generator (multi-config).
Unix presets use Ninja (single-config).

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `MCP_SDK_BUILD_TESTS` | ON | Build test suite |
| `MCP_SDK_BUILD_EXAMPLES` | ON | Build examples |
| `BUILD_SHARED_LIBS` | OFF | Build shared libraries |
| `MCP_SDK_ENABLE_TLS` | ON | Enable TLS (requires OpenSSL) |
| `MCP_SDK_ENABLE_AUTH` | ON | Enable OAuth features |
| `MCP_SDK_INTEGRATION_TESTS` | OFF | Build integration tests |

### Dependencies

Managed via vcpkg (requires `VCPKG_ROOT` env var): jsoncons, Boost.Asio, Boost.Beast, Boost.Process, OpenSSL, Catch2.

## Test Commands

### Run All Tests

```bash
ctest --test-dir build/vcpkg-unix-release                         # macOS/Linux
ctest --test-dir build/vcpkg-windows-release -C Release -j4       # Windows
```

### Run Single Test

```bash
ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_smoke_test -V

ctest --test-dir build/vcpkg-unix-release -R "jsonrpc_messages" -V

# Run test executable directly
./build/vcpkg-unix-release/tests/mcp_sdk_test_smoke
```

Windows (multi-config):
```bash
ctest --test-dir build/vcpkg-windows-release -C Release -R mcp_sdk_smoke_test -V
./build/vcpkg-windows-release/tests/Release/mcp_sdk_test_smoke.exe
```

### Run Tests by Tag

```bash
ctest --test-dir build/vcpkg-unix-release -L server
```

## Lint / Format Commands

```bash
cmake --build build/vcpkg-unix-release --target clang-format       # auto-format
cmake --build build/vcpkg-unix-release --target clang-format-check  # check only
```

clang-tidy runs automatically during build if installed. Resolve all warnings before committing.

## Code Style

### Language

- C++20, CMake 3.16+
- `.cpp` source, `.hpp` headers
- `#pragma once` for header guards

### Formatting (clang-format)

- 2-space indent, no tabs, 180 column limit
- Allman braces (opening brace on new line)
- Right-aligned pointers: `const char *ptr`, `int *count`
- Trailing commas in wrapped constructs
- One argument/parameter per line in calls and declarations
- `AlwaysBreakTemplateDeclarations: Yes`

### Include Order

1. C++ standard library (alphabetical)
2. Third-party (`<boost/...>`, `<catch2/...>`, `<jsoncons/...>`)
3. Project public headers (`<mcp/...>`)
4. Project private headers (`"mcp/..."`)

Empty line between each group. clang-format enforces this via `IncludeBlocks: Regroup`.

In `.cpp` files, always include the corresponding `.hpp` first among project headers using quoted form: `"mcp/foo/bar.hpp"`.

### Naming (clang-tidy enforced)

| Category | Style | Example |
|----------|-------|---------|
| Classes / Structs | `CamelCase` | `Server`, `JsonRpcError` |
| Functions / Methods | `camelBack` | `getVersion()`, `handleRequest()` |
| Private members | `camelBack_` (suffix) | `connectionState_`, `process_` |
| Class/static constants | `kCamelBack` (prefix k) | `kMaxRetries`, `kSchemaChunkSize` |
| Enum types | `CamelCase` | `TaskStatus` |
| Enum values | `kCamelCase` (prefix k) | `kWorking`, `kInputRequired` |
| Namespaces | `lower_case` | `mcp::server`, `mcp::detail` |
| Macros | `UPPER_CASE` | `MCP_SDK_ENABLE_TLS` |
| Template params | `CamelCase` | `ProcessType`, `Allocator` |

### Return Types

Use trailing return type syntax consistently:
```cpp
auto getVersion() const -> std::string_view;
auto create(ServerConfiguration config) -> std::shared_ptr<Server>;
static auto parseMessage(std::string_view json) -> jsonrpc::Message;
```

### Error Handling

- Exceptions for exceptional conditions (`std::runtime_error`, `std::invalid_argument`, `std::logic_error`)
- RAII and smart pointers (`std::unique_ptr`, `std::shared_ptr`) - no raw `new`/`delete`
- `const` correctness aggressively: mark parameters, methods, and locals `const`
- `noexcept` on trivial accessors and pure functions
- `std::optional<T>` for nullable return values (not raw pointers or magic values)
- Prefer `enum class` over `bool` for binary function parameters (clang-tidy enforced)

### Platform Specificity

- Guard POSIX headers with `#ifndef _WIN32`: `<unistd.h>`, `<fcntl.h>`, `<signal.h>`, `<sys/random.h>`
- On Windows: `#define NOMINMAX` before `<windows.h>`; include `<windows.h>` before `<bcrypt.h>`
- Use `#if defined(_WIN32)` for platform branching, not `ifdef _WIN32`
- Never name local variables `stdin`, `stdout`, or `stderr` (MSVC macro collision) - use `stdinInput`, `stdoutCapture`, `stderrOutput`

### Conventions

- Use `auto` with trailing return type for function declarations
- Use `std::string_view` for non-owning string parameters
- Use `std::size_t` (not `unsigned`) for sizes and counts
- Use `std::int64_t` / `std::uint64_t` for JSON-RPC integer types
- Catch all exceptions in destructors with `catch (...)`
- Use `std::scoped_lock` / `std::unique_lock` for mutexes
- Use `[[nodiscard]]` where return value must not be ignored (clang-tidy may suggest)

## Project Structure

```
include/mcp/        Public headers (organized by module)
src/                Implementation files (mirrors include structure)
tests/              Catch2 test files
examples/           Example applications
cmake/              CMake modules (Options.cmake, Dependencies.cmake)
.tools/checks/      Python-based codebase enforcement checks
vcpkg/ports/        vcpkg overlay ports
```

## Testing

- Framework: Catch2 v3 (`#include <catch2/catch_test_macros.hpp>`)
- Use `TEST_CASE("description", "[tag]")` with descriptive tags
- Test naming: `mcp_sdk_test_<module>` executable, test names in CTest follow `mcp_sdk_<module>_test`
- Integration tests (external SDKs) are opt-in via `MCP_SDK_INTEGRATION_TESTS=ON`
