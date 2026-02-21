# Dependency Graph

## Phase 1: Foundation (Must complete first)
- [ ] `task-007`: Extract Streamable HTTP Client Transport
- [ ] `task-008`: Introduce Shared HTTP Header State (Depends on: `task-007`)
- [ ] `task-009`: Introduce Unified Transport Inbound Loop Abstraction (Depends on: `task-007`)
- [ ] `task-001`: Define GET Listen Configuration Surface (Depends on: `task-007`, `task-008`)

## Phase 2: Transport Correctness (Blocked by Phase 1)
- [ ] `task-002`: Implement Default SSE Retry Waiting (Depends on: `task-001`)
- [ ] `task-003`: Make StreamableHttpClient Thread-Safe (Depends on: `task-002`, `task-008`)

## Phase 3: Client Integration (Blocked by Phase 2)
- [ ] `task-004`: Integrate GET Listen Loop Into StreamableHttpClientTransport (Depends on: `task-003`, `task-009`)

## Phase 4: Tests And Examples (Blocked by Phase 3)
- [ ] `task-005`: Add End-to-End Test For Server-Initiated Requests Over HTTP (Depends on: `task-004`)
- [ ] `task-006`: Update Examples And Documentation For HTTP Listen (Depends on: `task-004`)
