# Dependency Graph

## Phase 1: Scaffolding (Must complete first)
- [ ] `task-001`: Scaffold New Unit Test Targets

## Phase 2: Helper Contract Tests (Parallel; blocked by Phase 1)
- [ ] `task-002`: Unit Tests: Cancellation Helpers (Depends on: `task-001`)
- [ ] `task-003`: Unit Tests: Progress Helpers (Depends on: `task-001`)
- [ ] `task-004`: Unit Tests: Pinned Schema + SDK Version Helpers (Depends on: `task-001`)
- [ ] `task-005`: Unit Tests: Crypto RNG + Runtime Limits Defaults (Depends on: `task-001`)

## Phase 3: Core Semantics Tests (Parallel; blocked by Phase 1)
- [ ] `task-006`: Expand Unit Tests: Schema Validator (Depends on: `task-001`)
- [ ] `task-007`: Expand Unit Tests: JSON-RPC Messages (Depends on: `task-001`)
- [ ] `task-008`: Expand Unit Tests: JSON-RPC Router (Depends on: `task-001`)
- [ ] `task-009`: Expand Unit Tests: Lifecycle State Machine (Depends on: `task-001`)
- [ ] `task-010`: Expand Unit Tests: Tasks Store + Receiver (Depends on: `task-001`)

## Phase 4: Transport Tests (Parallel; blocked by Phase 1)
- [ ] `task-011`: Expand Unit Tests: Stdio Transport (Depends on: `task-001`)
- [ ] `task-012`: Expand Unit Tests: Stdio Subprocess Client (Depends on: `task-001`)
- [ ] `task-013`: Expand Unit Tests: Streamable HTTP Common Validation (Depends on: `task-001`)
- [ ] `task-014`: Expand Unit Tests: Streamable HTTP Client (Depends on: `task-001`)
- [ ] `task-015`: Expand Unit Tests: Streamable HTTP Server (Depends on: `task-001`)
- [ ] `task-016`: Expand Unit Tests: HTTP Runtime (URL/TLS error paths) (Depends on: `task-001`)

## Phase 5: Authorization Tests (Parallel; blocked by Phase 1)
- [ ] `task-017`: Expand Unit Tests: Protected Resource Metadata + Challenge Parsing (Depends on: `task-001`)
- [ ] `task-018`: Expand Unit Tests: Client Registration (Depends on: `task-001`)
- [ ] `task-019`: Expand Unit Tests: OAuth Client (PKCE/Step-up/Redirect policy) (Depends on: `task-001`)

## Phase 6: Facade-Level Edge Tests (Parallel; blocked by Phase 1)
- [ ] `task-020`: Expand Unit Tests: Client Facade (Depends on: `task-001`)
- [ ] `task-021`: Expand Unit Tests: Server Facade (Depends on: `task-001`)

## Phase 7: CI / Variant Builds (Optional follow-up)
- [ ] `task-022`: CI: Add Feature-Matrix Builds + Test Selection (Depends on: `task-006`, `task-013`, `task-017`, `task-019`)
