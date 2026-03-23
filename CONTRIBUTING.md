# Contributing to MCP C++ SDK

Thank you for contributing to the MCP C++ SDK! This document outlines the project's design philosophy and coding conventions.

## Design Philosophy

### Modern C++ Over Traditional OOP

This codebase follows **modern C++ best practices** that prioritize composition over inheritance:

- **Classes with state and behavior**: `Server`, `Client`, `Session`, `Transport` interface
- **Data-carrying structs**: Configuration, options, results, contexts use `struct` with public members
- **Functional callbacks**: Handlers use `std::function` instead of virtual method interfaces
- **Minimal inheritance**: Only for interfaces (e.g., `Transport`) and exception hierarchies

**Why this pattern?**

```cpp
// ✅ Preferred: Data struct + free function / std::function
struct ToolDefinition {
  std::string name;
  std::string description;
  jsonrpc::JsonValue inputSchema;
};

using ToolHandler = std::function<CallToolResult(const ToolCallContext&)>;

// ❌ Avoid: Unnecessary class with getters/setters
class ToolDefinition {
private:
  std::string name_;
public:
  auto getName() const -> const std::string& { return name_; }
};
```

**Benefits:**
- Better cache locality (contiguous data)
- No vtable overhead
- Easier testing (inject `std::function` mocks)
- Clearer ownership semantics
- Less boilerplate

**When to use classes:**
- Encapsulating mutable state with invariants
- RAII resource management
- Polymorphic behavior (via interfaces)
- When methods operate on instance state

**When to use structs:**
- Passive data containers (DTOs, configuration, results)
- Schema-mapped types (JSON-RPC messages, MCP protocol types)
- Value types with no invariants

### Naming Conventions

Follow the established patterns from `AGENTS.md`:

| Category | Style | Example |
|----------|-------|---------|
| Classes | `CamelCase` | `Server`, `Client` |
| Structs | `CamelCase` | `ToolDefinition`, `ServerConfiguration` |
| Functions | `camelBack` | `handleRequest()`, `create()` |
| Private members | `camelBack_` | `session_`, `configuration_` |
| Enums (type) | `CamelCase` | `TaskStatus` |
| Enums (values) | `kCamelCase` | `kWorking`, `kInputRequired` |
| Namespaces | `lower_case` | `mcp::server`, `mcp::detail` |

### Threading and Ownership

Document thread-safety in class/struct comments:

```cpp
/**
 * @par Thread-Safety Classification: Thread-safe
 * 
 * The Client class provides thread-safe access to all public methods.
 * Internal synchronization is provided via mutex_.
 */
class Client {
  // ...
};
```

**Ownership rules:**
- Use `std::unique_ptr` for exclusive ownership
- Use `std::shared_ptr` for shared ownership (e.g., `Session`)
- Use `std::string_view` for non-owning string parameters
- Use `std::optional<T>` for nullable returns (not raw pointers)

## Code Style

### Formatting

- **2-space indent**, no tabs
- **180 column limit**
- **Allman braces** (opening brace on new line)
- **Trailing commas** in wrapped constructs
- **One parameter per line** in function declarations

Run formatting before committing:
```bash
cmake --build build/vcpkg-unix-release --target clang-format
```

### Include Order

1. C++ standard library (alphabetical)
2. Third-party (`<boost/...>`, `<catch2/...>`, `<jsoncons/...>`)
3. Project public headers (`<mcp/...>`)
4. Project private headers (`"mcp/..."`)

Empty line between each group. clang-format enforces this.

### Error Handling

- **Exceptions** for exceptional conditions (`std::runtime_error`, `std::invalid_argument`)
- **RAII** throughout - no raw `new`/`delete`
- **`noexcept`** on trivial accessors and destructors
- **`[[nodiscard]]`** where return value must not be ignored

### Platform Specificity

```cpp
// ✅ Correct
#ifndef _WIN32
  #include <unistd.h>
#endif

#if defined(_WIN32)
  // Windows-specific code
#endif

// ❌ Avoid
#ifdef _WIN32
```

## Testing

- **Framework**: Catch2 v3
- **Test naming**: `mcp_sdk_test_<module>` executable
- **Tags**: Use descriptive tags `[server]`, `[client]`, `[transport]`
- **Thread safety**: Test concurrent access for thread-safe components

Run tests:
```bash
ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_server_test -V
```

## Pull Request Guidelines

1. **Small, focused changes** - One feature/fix per PR
2. **Update tests** - Add tests for new functionality
3. **Run checks** - `cmake --build ... --target clang-format-check`
4. **Document threading** - Add thread-safety notes for new classes
5. **Follow conventions** - Match existing code style

## Questions?

If you're unsure about the design pattern for a new feature, open a discussion issue first. We'd rather clarify upfront than refactor later.

## References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Abseil Style Guide](https://abseil.io/tips/)
- [AGENTS.md](AGENTS.md) - Detailed coding standards
