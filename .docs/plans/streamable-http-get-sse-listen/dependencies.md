# Dependency Graph

## Phase 1: Foundation (Must complete first)
- [ ] `task-001`: Define GET Listen Configuration Surface

## Phase 2: Transport Correctness (Blocked by Phase 1)
- [ ] `task-002`: Implement Default SSE Retry Waiting (Depends on: `task-001`)
- [ ] `task-003`: Make StreamableHttpClient Thread-Safe (Depends on: `task-002`)

## Phase 3: Client Integration (Blocked by Phase 2)
- [ ] `task-004`: Integrate GET Listen Loop Into StreamableHttpClientTransport (Depends on: `task-003`)

## Phase 4: Tests And Examples (Blocked by Phase 3)
- [ ] `task-005`: Add End-to-End Test For Server-Initiated Requests Over HTTP (Depends on: `task-004`)
- [ ] `task-006`: Update Examples And Documentation For HTTP Listen (Depends on: `task-004`)
