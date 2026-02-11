# Integration Tests (Reference SDK Interop)

This directory contains end-to-end integration tests that exercise interoperability between this C++ SDK and the official Python reference SDK over Streamable HTTP.

## Pinned Reference SDK Versions

- `mcp==1.26.0`
- `httpx==0.28.1`

The pinned versions are captured in `tests/integration/fixtures/reference_python_requirements.txt`.

## Coverage

- Reference Python client -> C++ SDK server fixture
  - unauthenticated initialize expected to fail (401 behavior)
  - authenticated initialize succeeds
  - tools/resources/prompts work end-to-end
- C++ SDK client fixture -> reference Python server
  - unauthenticated initialize expected to fail
  - authenticated initialize succeeds
  - tools/resources/prompts work end-to-end
