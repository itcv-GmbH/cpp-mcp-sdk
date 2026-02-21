# Thread-Safety Contract

## Purpose

This document must define the thread-safety contract for the SDK.

This contract is required to eliminate undefined behavior under concurrent use and is required to make the concurrency guarantees of the SDK auditable.

## Scope

- This contract must cover every public type and public entrypoint that participates in any of the following:
  - concurrent in-flight request routing
  - background thread creation (`std::thread` and `mcp::detail::InboundLoop`)
  - work scheduling onto `boost::asio::thread_pool`
  - invocation of user-provided callbacks
- This contract must treat all other public types as `Thread-compatible` unless this document assigns a stronger classification.

## Contract Requirements

- Every public type that is part of the SDK API surface will declare a thread-safety classification in its public header.
- Every public method will declare its thread-safety requirements in its public header.
- Every callback type exposed by the SDK will declare the threads on which it will be invoked.
- Every internal mutex will have a documented lock ordering rule.

## Required Classifications

- "Thread-safe" will mean that all documented entrypoints are safe under concurrent invocation from multiple threads.
- "Thread-compatible" will mean that concurrent invocation is not supported, and external serialization is required.
- "Thread-confined" will mean that all entrypoints are required to be called from a single designated thread, and the designated thread will be documented.

## Covered Types (Minimum Set)

This contract must include explicit classifications and method-level rules for the following types at minimum:

- `mcp::Client`
- `mcp::Session`
- `mcp::jsonrpc::Router`
- `mcp::transport::Transport` and all concrete `Transport` implementations used by the SDK
- `mcp::transport::HttpServerRuntime` and `mcp::transport::HttpClientRuntime`
- `mcp::transport::http::StreamableHttpClient` and `mcp::transport::http::StreamableHttpServer`
- `mcp::StdioServerRunner`, `mcp::StreamableHttpServerRunner`, and `mcp::CombinedServerRunner`

## Callback Threading Rules

- Every callback type exposed by the SDK must declare whether the SDK invokes callbacks serially or concurrently.
- Every callback type exposed by the SDK must declare whether the SDK invokes the callback on an I/O thread, an internal worker thread, or an application-provided executor.
- The `mcp::SessionOptions.threading` configuration is required to define the default callback threading behavior for session and client handler callbacks.

## Global Rules

- `start()` and `stop()` methods for SDK-owned runtimes and transports will be idempotent.
- `stop()` methods that are declared `noexcept` will never throw.
- Destructors will never throw.
