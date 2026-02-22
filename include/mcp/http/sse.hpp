#pragma once

/**
 * @file sse.hpp
 * @brief Server-Sent Events (SSE) types and utilities for MCP HTTP transport.
 *
 * This is an umbrella header that includes all SSE-related headers.
 * For fine-grained includes, use the individual headers directly.
 *
 * @section Types
 * - EventIdCursor: Cursor for tracking position in an SSE stream
 * - Event: Single SSE event with optional fields
 *
 * @section Functions
 * - makeEventId(): Create an event ID from stream ID and cursor
 * - parseEventId(): Parse an event ID into its components
 * - encodeEvent(): Encode a single event to SSE format
 * - encodeEvents(): Encode multiple events to SSE format
 * - parseEvents(): Parse SSE-formatted data into events
 */

#include <mcp/http/detail.hpp>
#include <mcp/http/encoding.hpp>
#include <mcp/http/event.hpp>
#include <mcp/http/event_id_cursor.hpp>
