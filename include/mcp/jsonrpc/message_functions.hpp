#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <mcp/jsonrpc/encode_options.hpp>
#include <mcp/jsonrpc/message.hpp>
#include <mcp/jsonrpc/message_validation_error.hpp>
#include <mcp/jsonrpc/types.hpp>
#include <mcp/sdk/errors.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Parse a JSON-RPC message from a JSON string.
 *
 * @param utf8Json UTF-8 encoded JSON string
 * @return Parsed Message variant
 * @throws MessageValidationError on invalid JSON or malformed JSON-RPC message
 *
 * @subsection Exceptions
 * Throws MessageValidationError on:
 * - Invalid JSON syntax
 * - Missing required JSON-RPC fields (jsonrpc, method for requests)
 * - Type mismatches in message structure
 */
auto parseMessage(std::string_view utf8Json) -> Message;

/**
 * @brief Parse a JSON-RPC message from a jsoncons value.
 *
 * @param messageJson Pre-parsed JSON value
 * @return Parsed Message variant
 * @throws MessageValidationError for invalid message structure
 */
auto parseMessageJson(const JsonValue &messageJson) -> Message;

/**
 * @brief Convert a Message to a jsoncons JSON value.
 *
 * @param message Message to convert
 * @return JSON value representation
 */
auto toJson(const Message &message) -> JsonValue;

/**
 * @brief Serialize a Message to a JSON string.
 *
 * @param message Message to serialize
 * @param options Serialization options
 * @return JSON string
 * @throws std::runtime_error on serialization failure (rare)
 */
auto serializeMessage(const Message &message, const EncodeOptions &options = {}) -> std::string;

}  // namespace mcp::jsonrpc
