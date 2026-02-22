# Dependency Graph

## Phase 1: Tooling Foundation (Must complete first)
- [ ] `task-001`: Implement Namespace Layout Enforcement Check (Not Yet Enabled)

## Phase 2: Core Header and Namespace Normalization (Blocked by Phase 1)
- [ ] `task-002`: Normalize JSON-RPC Module Headers And Namespaces (Depends on: `task-001`)
- [ ] `task-003`: Normalize Schema Module Headers And Namespaces (Depends on: `task-001`)
- [ ] `task-004`: Normalize HTTP SSE Module Layout (Depends on: `task-001`)
- [ ] `task-005`: Normalize Transport Module Headers And Namespaces (Depends on: `task-002`, `task-004`)
- [ ] `task-006`: Normalize Security Module Detail Layout (Depends on: `task-001`)
- [ ] `task-007`: Normalize Auth Module Umbrella Headers (Depends on: `task-001`)
- [ ] `task-008`: Normalize Util Module Umbrella Headers (Depends on: `task-001`)
- [ ] `task-009`: Normalize SDK Module Namespaces And Remove Deprecated Top-Level Wrappers (Depends on: `task-001`)

## Phase 3: Role and Lifecycle Refactor (Blocked by Phase 2)
- [ ] `task-010`: Normalize Lifecycle Session Headers And Namespaces (Depends on: `task-002`, `task-003`, `task-005`, `task-009`)
- [ ] `task-011`: Normalize Server Module Headers, Runners, And Namespaces (Depends on: `task-002`, `task-005`, `task-010`)
- [ ] `task-012`: Normalize Client Module Headers And Namespaces (Depends on: `task-002`, `task-005`, `task-010`, `task-011`)

## Phase 4: Facades, Docs, And Enforcement Gates (Blocked by Phase 3)
- [ ] `task-013`: Add Top-Level Facades And Umbrella Headers (Depends on: `task-010`, `task-011`, `task-012`)
- [ ] `task-014`: Update Documentation, Examples, And Tests To Canonical Includes (Depends on: `task-013`)
- [ ] `task-015`: Enable Namespace Layout Enforcement In CI Checks (Depends on: `task-014`)
