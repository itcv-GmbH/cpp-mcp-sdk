# Dependency Graph

## Phase 1: API Design (Must complete first)
- [x] `task-001`: Define Runner Public API + Docs Updates
- [x] `task-002`: Define ServerFactory Contract + Session Isolation Rules

## Phase 2: Streamable HTTP Session Foundation (Blocked by Phase 1)
- [x] `task-014`: Implement Server-Issued MCP-Session-Id on Initialize (Transport)
- [x] `task-015`: Add Transport Conformance Tests for Session Issuance + Multi-Session Routing (Depends on: `task-014`)

## Phase 3: STDIO Runner (Blocked by Phase 1)
- [x] `task-003`: Implement STDIO Server Runner (Blocking + Async)
- [x] `task-004`: Add STDIO Runner Unit Tests (Depends on: `task-003`)

## Phase 4: Streamable HTTP Runner (Blocked by Phase 2)
- [x] `task-005`: Implement Streamable HTTP Server Runner (Per-Session Factory + Runtime) (Depends on: `task-014`)
- [x] `task-006`: Add HTTP Runner Unit Tests (Depends on: `task-005`)

## Phase 5: Examples + Documentation (Blocked by Phases 3-4)
- [x] `task-012`: Implement Combined Runner (Start STDIO, HTTP, or Both) (Depends on: `task-003`, `task-005`)
- [x] `task-013`: Add Combined Runner Unit Tests (Depends on: `task-012`)
- [x] `task-007`: Add Dual-Transport Example (STDIO + HTTP) (Depends on: `task-012`)
- [x] `task-008`: Migrate Existing Examples to Runners (Depends on: `task-007`)
- [x] `task-009`: Update Quickstarts + API Overview (Depends on: `task-008`)

## Phase 6: Integration / Interop Coverage (Blocked by Phases 2 and 4)
- [ ] `task-016`: Update Reference Interop Fixture to Use HTTP Runner (Depends on: `task-005`, `task-014`)
- [ ] `task-017`: Add Reference Interop Tests for STDIO Runner (Depends on: `task-003`)

## Phase 7: Build Wiring + Full Verification (Blocked by all prior phases)
- [ ] `task-010`: Wire New Sources/Tests into CMake (Depends on: `task-004`, `task-006`, `task-013`, `task-015`)
- [ ] `task-011`: Full Conformance + Regression Run (Depends on: `task-010`, `task-016`, `task-017`)
