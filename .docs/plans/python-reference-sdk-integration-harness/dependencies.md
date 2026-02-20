# Dependency Graph

## Phase 1: Foundation (Must complete first)
- [x] `task-001`: Define Coverage Matrix And Gate
- [x] `task-002`: Create Python Raw JSON-RPC Harness
- [x] `task-003`: Add Integration Test Wiring For New Suites (Depends on: `task-001`, `task-002`)

## Phase 2: C++ Fixtures (Blocked by Phase 1)
- [x] `task-004`: Implement C++ HTTP Utilities Fixture (Depends on: `task-002`)
- [x] `task-005`: Implement C++ HTTP Resources Advanced Fixture (Depends on: `task-002`)
- [x] `task-006`: Implement C++ HTTP Roots Fixture (Depends on: `task-002`)
- [x] `task-007`: Implement C++ HTTP Tasks Fixture (Depends on: `task-002`)
- [x] `task-008`: Implement C++ STDIO Utilities Fixture (Depends on: `task-002`)
- [x] `task-009`: Implement C++ STDIO Resources Advanced Fixture (Depends on: `task-002`)
- [x] `task-010`: Implement C++ STDIO Roots Fixture (Depends on: `task-002`)
- [x] `task-011`: Implement C++ STDIO Tasks Fixture (Depends on: `task-002`)

## Phase 3: Python Reference Client To C++ Server Tests (Blocked by Phase 2)
- [x] `task-012`: Add Python Client HTTP Coverage Tests (Depends on: `task-004`, `task-005`, `task-006`, `task-007`)
- [x] `task-013`: Add Python Client STDIO Coverage Tests (Depends on: `task-008`, `task-009`, `task-010`, `task-011`)

## Phase 4: C++ Client To Python Reference Server Tests (Blocked by Phase 1)
- [x] `task-014`: Expand Python Reference Server Fixture (Depends on: `task-002`)
- [x] `task-015`: Add C++ Client Fixtures For Full Surface (Depends on: `task-014`)
- [ ] `task-016`: Add C++ Client To Python Server Coverage Tests (Depends on: `task-015`)

## Phase 5: CI Gate (Blocked by Phase 3 And Phase 4)
- [ ] `task-017`: Add CI Job For Python Reference Integration Suite (Depends on: `task-012`, `task-013`, `task-016`)
