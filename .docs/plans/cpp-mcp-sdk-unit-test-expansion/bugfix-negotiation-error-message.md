# Bugfix: Negotiation Failure Error Message

## Rationale

Task-009 Step 3 requires actionable protocol negotiation failures. The prior server error message for negotiation failure when no versions are supported was static and did not include the client-requested version or the server-supported versions list, which blocked deterministic assertions and reduced debugging value.

## Change Summary

- Updated lifecycle initialization negotiation failure messaging to include:
  - requested protocol version in the form `requested '<version>'`
  - explicit supported versions detail in the form `supported: [..]`
- For empty supported versions, the message now renders `supported: []`.
- Existing structured JSON-RPC error `data` fields (`requested`, `supported`) remain intact.

## Risk Assessment

- Low risk: message-only behavior change in an existing error path.
- No protocol state transitions, negotiation selection logic, or success-path payload semantics were changed.
