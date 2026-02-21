# Thread-Safety Contract

## Purpose

This document will define the thread-safety contract for the SDK.

## Contract Requirements

- Every public type that is part of the SDK API surface will declare a thread-safety classification in its public header.
- Every public method will declare its thread-safety requirements in its public header.
- Every callback type exposed by the SDK will declare the threads on which it will be invoked.
- Every internal mutex will have a documented lock ordering rule.

## Required Classifications

- "Thread-safe" will mean that all documented entrypoints are safe under concurrent invocation from multiple threads.
- "Thread-compatible" will mean that concurrent invocation is not supported, and external serialization is required.

## Global Rules

- `start()` and `stop()` methods for SDK-owned runtimes and transports will be idempotent.
- `stop()` methods that are declared `noexcept` will never throw.
- Destructors will never throw.
