# Dependency Graph

## Phase 1: Foundations (Must complete first)
- [x] `task-001`: Add Internal ASCII Helpers + Tests
- [x] `task-002`: Add Internal Base64url Helpers + Tests
- [x] `task-003`: Add Internal Absolute URL Parser + Tests
- [x] `task-004`: Add Initialize/Capabilities JSON Codec + Tests

## Phase 2: DRY Refactors (Blocked by Phase 1)
- [x] `task-005`: Refactor Pagination Cursor Base64url (Depends on: `task-002`)
- [x] `task-006`: Refactor OAuth PKCE Base64url (Depends on: `task-002`)
- [x] `task-007`: Refactor HTTP Header/Origin ASCII Helpers (Depends on: `task-001`)
- [x] `task-008`: Refactor Auth ASCII + Absolute URL Parsing (Depends on: `task-001`, `task-003`)
- [x] `task-009`: Refactor HTTP Client Endpoint URL Parsing (Depends on: `task-001`, `task-003`)
- [x] `task-010`: Deduplicate Client Initialize JSON Building (Depends on: `task-004`)

## Phase 3: Stub / API Cleanup (Blocked by Phase 2)
- [x] `task-011`: Session Lifecycle API De-Slop (Depends on: `task-010`)
- [x] `task-012`: Remove or Hard-Disable HttpTransport Stub (Depends on: `task-011`)
- [x] `task-013`: Clarify StdioTransport Instance API (Depends on: `task-011`)

## Phase 4: Concurrency Hardening (Blocked by Phase 2)
- [ ] `task-014`: Fix StreamableHttpServer Handler Locking (Depends on: `task-007`)

## Phase 4b: Threading Hardening (Optional; Blocked by Phase 2)
- [ ] `task-016`: Router Detached Thread Removal (Depends on: `task-001`)
- [ ] `task-017`: Client Detached Thread Removal (Depends on: `task-016`)

## Phase 5: Final Gate (Blocked by Phases 3-4)
- [ ] `task-015`: Full Regression + Conformance Run (Depends on: `task-012`, `task-013`, `task-014`)
