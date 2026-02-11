# AGENTS.md - MCP C++ SDK Development Guide

## Project Overview
Model Context Protocol (MCP) SDK for C++ - A CMake-based C++17 library for implementing MCP servers and clients.

## Build Commands

### Configure & Build

**First-time setup (dependencies required):**
```bash
# Configure with vcpkg preset to install dependencies (jsoncons, Boost, etc.)
cmake --preset vcpkg-unix-release

# Build
cmake --build build/vcpkg-unix-release
```

**Subsequent builds (dependencies already cached):**
```bash
# Configure with preset (macOS/Linux) - uses cached vcpkg dependencies
cmake --preset unix-release

# Build
cmake --build build/unix-release
```

**Windows builds:**
```bash
cmake --preset windows-release
cmake --build build/windows-release --config Release
```

**Note:** The `unix-release` preset requires dependencies to be already installed or cached from a previous vcpkg build. Use `vcpkg-unix-release` preset for the first build or when dependencies are missing.

**IMPORTANT:** On UNIX systems, you can parallelize builds while reserving 4 CPU cores:
```bash
cmake --build build/unix-release --parallel $(( $(nproc) - 4 ))
```

### Build Options
- `MCP_SDK_BUILD_TESTS=ON` - Build test suite (default: ON)
- `MCP_SDK_BUILD_EXAMPLES=ON` - Build examples (default: ON)
- `BUILD_SHARED_LIBS=ON` - Build shared libraries (default: OFF)

## Test Commands

### Run All Tests
```bash
# Using CTest
cd build/unix-release && ctest

# Using CMake preset
cmake --preset unix-release && cmake --build build/unix-release && ctest --preset unix-release
```

### Run Single Test
```bash
# Run specific test by name
ctest --preset unix-release -R mcp_sdk_smoke_test -V

# Or run test executable directly
./build/unix-release/tests/mcp_sdk_test_smoke
```

## Lint/Format Commands

### Code Formatting (clang-format)
```bash
# Auto-format all source files
cmake --build build/unix-release --target clang-format

# Check formatting without modifying files
cmake --build build/unix-release --target clang-format-check
```

### Static Analysis (clang-tidy)
```bash
# Run during build (automatically enabled if clang-tidy is found)
cmake --build build/unix-release
```

## Code Style Guidelines

### Language & Standards
- **C++ Standard**: C++17 minimum
- **CMake**: 3.16+
- **File Extensions**: `.cpp` for source, `.hpp` for headers

### Naming Conventions
- **Classes/Structs**: `CamelCase` (e.g., `Server`, `ClientConnection`)
- **Functions/Methods**: `camelBack` (e.g., `getVersion()`, `handleRequest()`)
- **Private Members**: `camelBack` with `_` suffix (e.g., `connectionState_`)
- **Constants**: `kCamelBack` for class/static, `UPPER_CASE` for global
- **Namespaces**: `CamelCase` (e.g., `namespace mcp { namespace sdk {`)
- **Enums**: `CamelCase` for type, `CamelCase` for values
- **Macros**: `UPPER_CASE` with underscores
- **Template Parameters**: `CamelCase`

### Code Formatting (clang-format)
- **Indent**: 2 spaces (no tabs)
- **Line Length**: 180 characters
- **Braces**: Allman style (opening brace on new line)
- **Pointers**: Right-aligned (e.g., `const char *ptr`)
- **Includes**: Sorted with standard library first
- **Trailing Commas**: Insert in wrapped constructs

### Static Analysis (clang-tidy)
- Enabled checks: Most modern C++ checks enabled
- Warnings treated as errors: None by default
- Key checks: cppcoreguidelines-, bugprone-, readability-, performance-
- Disabled: fuchsia-*, llvm-header-guard, llvm-include-order
- Never commit before all clang-tidy warnings have been resolved

### Error Handling
- Use exceptions for exceptional conditions
- Prefer RAII and smart pointers over manual memory management
- Use `const` correctness aggressively
- Prefer `enum class` over `bool` for binary parameters

### Project Structure
```
include/mcp/sdk/  - Public headers
src/              - Implementation files
examples/         - Example code
tests/            - Test files
cmake/            - CMake modules
```

### Header Guards
Use `#pragma once` (preferred over include guards in this project)

### Documentation
- Document public API in header files
- Use clear, descriptive function/variable names
- Add comments for complex logic or non-obvious decisions
