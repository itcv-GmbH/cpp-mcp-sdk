# Dependency Graph

## Phase 1: API Design (Must complete first)
- [ ] `task-001`: Define Runner Public API + Docs Updates
- [ ] `task-002`: Define ServerFactory Contract + Session Isolation Rules

## Phase 2: STDIO Runner (Blocked by Phase 1)
- [ ] `task-003`: Implement STDIO Server Runner (Blocking + Optional Async)
- [ ] `task-004`: Add STDIO Runner Unit Tests (Depends on: `task-003`)

## Phase 3: Streamable HTTP Runner (Blocked by Phase 1)
- [ ] `task-005`: Implement Streamable HTTP Server Runner (Factory + Runtime)
- [ ] `task-006`: Add HTTP Runner Unit Tests (Depends on: `task-005`)

## Phase 4: Examples + Documentation (Blocked by Phases 2-3)
- [ ] `task-012`: Implement Combined Runner (Start STDIO, HTTP, or Both) (Depends on: `task-003`, `task-005`)
- [ ] `task-013`: Add Combined Runner Unit Tests (Depends on: `task-012`)
- [ ] `task-007`: Add Dual-Transport Example (STDIO + HTTP) (Depends on: `task-012`)
- [ ] `task-008`: Migrate Existing Examples to Runners (Depends on: `task-007`)
- [ ] `task-009`: Update Quickstarts + API Overview (Depends on: `task-008`)

## Phase 5: Build Wiring + Full Verification (Blocked by all prior phases)
- [ ] `task-010`: Wire New Sources/Tests into CMake (Depends on: `task-004`, `task-006`, `task-013`)
- [ ] `task-011`: Full Conformance + Regression Run (Depends on: `task-010`)
