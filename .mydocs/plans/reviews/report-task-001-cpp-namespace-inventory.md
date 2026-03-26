# Review Report: task-001 (/ Inventory `src/**/*.cpp` namespace-to-path violations)

## Status
**FAIL**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [ ] Implementation matches `task-[id].md` instructions.
- [ ] Definition of Done met.
- [x] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `python3 - <<'PY'  # scans src/**/*.cpp, extracts first opened mcp namespace, compares to directory-derived expected namespace per SRS FR2
...script omitted for brevity...
PY`
*   **Result:** Fail. 5 namespace-to-path violations were reproduced in `src/**/*.cpp`: `src/server/server.cpp` (`mcp::server::detail` vs `mcp::server`), `src/transport/http_client.cpp`, `src/transport/http_runtime.cpp`, `src/transport/http_server.cpp` (`mcp::transport::http` vs `mcp::transport`), and `src/version.cpp` (`mcp::sdk` vs `mcp`).

## Issues Found (If FAIL)
*   **Critical:** Namespace derivation logic in `task-001-inventory.md` does not follow SRS FR2 (`directory segments only, filename excluded`). It incorrectly treats `src/transport/http_*.cpp` as `mcp::transport::http` and `src/version.cpp` as `mcp::sdk`, causing real violations to be missed.
*   **Major:** `task-001-inventory.md` does not record the first MCP namespace opened for `src/server/server.cpp`; first opened is `mcp::server::detail` (line 85), not `mcp::server`.
*   **Minor:** The report conclusion `NO VIOLATIONS FOUND` is not reproducible with a deterministic path-derived check from a clean checkout.

## Required Actions
1. Regenerate `task-001-inventory.md` using strict path-derived namespace expectations from SRS FR2 (`src/<segments>/file.cpp` => `mcp::<segments>`, root `src/*.cpp` => `mcp`).
2. Update inventory entries to capture the first MCP namespace actually opened in each file and include all violating translation units.
3. Replace the `NO VIOLATIONS FOUND` summary with the reproduced violation set and keep the verification procedure deterministic.
