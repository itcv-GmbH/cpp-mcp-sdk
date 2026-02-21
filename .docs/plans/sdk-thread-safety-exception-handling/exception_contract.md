# Exception Contract

## Purpose

This document will define the exception contract for the SDK.

## Contract Requirements

- Public methods will document whether they will throw exceptions.
- All destructors will be `noexcept` and will never throw.
- All SDK-created thread entrypoints will be `noexcept` and will catch all exceptions.
- Protocol-level failures will be represented as JSON-RPC error responses when the protocol requires a JSON-RPC response.
- Transport-level failures will be represented as C++ exceptions at the transport API boundary and will be reported via unified error reporting when failures occur on background threads.
