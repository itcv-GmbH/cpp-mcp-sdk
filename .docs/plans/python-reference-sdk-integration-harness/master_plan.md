# Python Reference SDK Integration Harness

This plan will deliver a comprehensive, automated integration test harness that exercises the full MCP 2025-11-25 protocol surface against the official Python reference SDK.

## ADR (Architecture Decision Record)

### ADR-PRIH-001: CTest-driven Python scripts
- The harness will execute Python scripts via CTest entries defined in `tests/integration/CMakeLists.txt`.
- Each CTest entry will run one script that performs a bounded set of protocol assertions and exits non-zero on failure.

### ADR-PRIH-002: Pinned, build-local Python environment
- The harness will create and use a Python virtual environment under the build directory (`build/**/tests/integration/python-venv`).
- The harness will install pinned dependencies from `tests/integration/fixtures/reference_python_requirements.txt`.

### ADR-PRIH-003: Raw JSON-RPC harness layer
- The harness will implement a raw JSON-RPC send/receive layer in Python for Streamable HTTP and STDIO.
- The raw layer will be responsible for capturing notifications and validating ordering and correlation rules required by the MCP specification.
- The raw layer will be used for protocol surfaces that are not exposed by the Python reference SDK high-level APIs.

### ADR-PRIH-004: Dedicated fixture executables
- The harness will build dedicated C++ fixture executables under `tests/integration/`.
- Each fixture executable will provide deterministic, test-specific behavior and will emit machine-parseable readiness and diagnostics to stdout.

### ADR-PRIH-005: Full surface coverage gate
- The harness will include a coverage matrix file that maps every required MCP 2025-11-25 request and notification name listed in the SRS to at least one automated integration test.
- The harness will fail CI when the coverage matrix contains any uncovered protocol surface.

## Target Files (Planned Touch List)

### Build Wiring
- `tests/CMakeLists.txt`
- `tests/integration/CMakeLists.txt`

### Integration Test Documentation
- `tests/integration/README.md`
- `tests/integration/COVERAGE.md`

### Python Harness Code
- `tests/integration/python/__init__.py`
- `tests/integration/python/harness.py`
- `tests/integration/python/streamable_http_raw.py`
- `tests/integration/python/stdio_raw.py`
- `tests/integration/python/assertions.py`

### Python Integration Test Scripts (Existing)
- `tests/integration/scripts/reference_client_to_cpp_server.py`
- `tests/integration/scripts/reference_client_to_cpp_stdio_server.py`
- `tests/integration/scripts/cpp_client_to_reference_server.py`

### Python Integration Test Scripts (To Be Added)
- `tests/integration/scripts/reference_coverage_gate.py`
- `tests/integration/scripts/reference_client_to_cpp_server_utilities.py`
- `tests/integration/scripts/reference_client_to_cpp_server_resources_advanced.py`
- `tests/integration/scripts/reference_client_to_cpp_server_roots.py`
- `tests/integration/scripts/reference_client_to_cpp_server_tasks.py`
- `tests/integration/scripts/reference_client_to_cpp_stdio_server_utilities.py`
- `tests/integration/scripts/reference_client_to_cpp_stdio_server_resources_advanced.py`
- `tests/integration/scripts/reference_client_to_cpp_stdio_server_roots.py`
- `tests/integration/scripts/reference_client_to_cpp_stdio_server_tasks.py`
- `tests/integration/scripts/cpp_client_to_reference_server_utilities.py`
- `tests/integration/scripts/cpp_client_to_reference_server_resources_advanced.py`
- `tests/integration/scripts/cpp_client_to_reference_server_roots.py`
- `tests/integration/scripts/cpp_client_to_reference_server_tasks.py`

### C++ Fixture Executables (Existing)
- `tests/integration/cpp_server_fixture.cpp`
- `tests/integration/cpp_stdio_server_fixture.cpp`
- `tests/integration/cpp_client_fixture.cpp`

### C++ Fixture Executables (To Be Added)
- `tests/integration/cpp_server_utilities_fixture.cpp`
- `tests/integration/cpp_server_resources_advanced_fixture.cpp`
- `tests/integration/cpp_server_roots_fixture.cpp`
- `tests/integration/cpp_server_tasks_fixture.cpp`
- `tests/integration/cpp_stdio_server_utilities_fixture.cpp`
- `tests/integration/cpp_stdio_server_resources_advanced_fixture.cpp`
- `tests/integration/cpp_stdio_server_roots_fixture.cpp`
- `tests/integration/cpp_stdio_server_tasks_fixture.cpp`
- `tests/integration/cpp_client_utilities_fixture.cpp`
- `tests/integration/cpp_client_resources_advanced_fixture.cpp`
- `tests/integration/cpp_client_roots_fixture.cpp`
- `tests/integration/cpp_client_tasks_fixture.cpp`

### Python Reference Server Fixture
- `tests/integration/fixtures/reference_python_server.py`

### CI
- `.github/workflows/ci.yml`

## Verification Strategy

- The implementer must run a clean build with integration tests enabled:
  - `rm -rf build/vcpkg-unix-release`
  - `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
  - `cmake --build build/vcpkg-unix-release`

- The implementer must run all Python reference integration tests:
  - `ctest --test-dir build/vcpkg-unix-release -R integration_reference --output-on-failure`

- The implementer must verify coverage completeness:
  - `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_integration_reference_coverage_gate --output-on-failure`

## Scope Guardrails (Out of Scope)

- This plan will not change SDK runtime behavior to satisfy tests.
- This plan will not add external network dependencies beyond localhost transport exercised by the fixtures.
- This plan will not modify the normative MCP spec mirror under `.docs/requirements/mcp-spec-2025-11-25/`.

## Risks / Unknowns

- The Python reference SDK APIs for some MCP utilities (tasks, progress, cancellation, logging, completion, subscriptions) are required to be validated against `mcp==1.26.0`.
- The harness is required to remain deterministic across macOS, Linux, and Windows, and the implementer will be responsible for removing sleep-based flakiness.
- Streamable HTTP notification ordering and SSE reconnection semantics are required to be verified with raw transport tooling, and the implementer will be responsible for handling SSE framing differences.

## SRS Sources of Truth

- Primary SRS: `.docs/requirements/cpp-mcp-sdk.md`
- Normative spec mirror: `.docs/requirements/mcp-spec-2025-11-25/`
