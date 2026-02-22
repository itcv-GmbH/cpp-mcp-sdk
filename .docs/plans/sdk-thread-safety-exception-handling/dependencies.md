# Dependency Graph

## Phase 1: Contracts (Must complete first)
- [x] `task-001`: Define Thread-Safety Contract
- [x] `task-002`: Define Exception Contract

## Phase 2: Infrastructure (Blocked by Phase 1)
- [ ] `task-003`: Add Unified Error Reporting Mechanism (Depends on: `task-001`, `task-002`)
- [ ] `task-004`: Enforce No-Throw Thread Boundaries (Depends on: `task-003`)

## Phase 3: Module Alignment (Blocked by Phase 2)
- [ ] `task-005`: Align JSON-RPC Router Threading And Errors (Depends on: `task-004`)
- [ ] `task-006`: Align Transports And Runners Threading And Errors (Depends on: `task-004`)
- [ ] `task-007`: Align Client And Session Threading And Errors (Depends on: `task-004`)

## Phase 4: Verification Gates (Blocked by Phase 3)
- [ ] `task-008`: Add Deterministic Concurrency And Exception Tests (Depends on: `task-005`, `task-006`, `task-007`)
