#pragma once

/**
 * @file messages.hpp
 * @brief Umbrella header for JSON-RPC message types.
 *
 * @section Overview
 * This header provides access to all JSON-RPC message types, utilities, and error handling.
 * For specific types, you can include individual headers from the jsonrpc/ directory.
 *
 * @subsection Type Categories
 *
 * @subsubsection Core Types
 * - types.hpp: RequestId, JsonValue
 * - encode_options.hpp: EncodeOptions
 * - message_validation_error.hpp: MessageValidationError
 *
 * @subsubsection Message Types
 * - request_context.hpp: RequestContext
 * - request.hpp: Request
 * - notification.hpp: Notification
 * - success_response.hpp: SuccessResponse
 * - error_response.hpp: ErrorResponse
 * - response.hpp: Response (variant alias)
 * - message.hpp: Message (variant alias)
 *
 * @subsubsection Functions
 * - message_functions.hpp: parseMessage, parseMessageJson, toJson, serializeMessage
 * - error_factories.hpp: makeJsonRpcError, makeParseError, etc.
 * - response_factories.hpp: makeErrorResponse, makeUnknownIdErrorResponse
 *
 * @section Exceptions
 *
 * @subsection Exception Types
 * - MessageValidationError: Thrown when parsing fails due to malformed JSON-RPC messages
 *   Inherits from std::runtime_error
 *
 * @subsection Parsing Operations (throwing)
 * - parseMessage(std::string_view) throws MessageValidationError on:
 *   - Invalid JSON syntax
 *   - Missing required JSON-RPC fields (jsonrpc, method for requests)
 *   - Type mismatches in message structure
 * - parseMessageJson(const JsonValue&) throws MessageValidationError for invalid structure
 *
 * @subsection Serialization Operations
 * - toJson(const Message&) returns JsonValue
 * - serializeMessage() throws std::runtime_error on serialization failure (rare)
 *
 * @subsection Error Factory Functions
 * All make*Error() and make*ErrorResponse() functions return by value:
 * - makeJsonRpcError(), makeParseError(), makeInvalidRequestError()
 * - makeMethodNotFoundError(), makeInvalidParamsError(), makeInternalError()
 * - makeUrlElicitationRequiredError(), makeErrorResponse(), makeUnknownIdErrorResponse()
 *
 * @subsection JSON-RPC Error vs C++ Exception
 * Protocol-level errors (method not found, invalid params) are represented as ErrorResponse
 * objects, not C++ exceptions. C++ exceptions indicate:
 * - Parse failures (malformed JSON)
 * - System errors (memory exhaustion)
 */

#include <mcp/jsonrpc/encode_options.hpp>
#include <mcp/jsonrpc/error_factories.hpp>
#include <mcp/jsonrpc/error_response.hpp>
#include <mcp/jsonrpc/message.hpp>
#include <mcp/jsonrpc/message_functions.hpp>
#include <mcp/jsonrpc/message_validation_error.hpp>
#include <mcp/jsonrpc/notification.hpp>
#include <mcp/jsonrpc/request.hpp>
#include <mcp/jsonrpc/request_context.hpp>
#include <mcp/jsonrpc/response.hpp>
#include <mcp/jsonrpc/response_factories.hpp>
#include <mcp/jsonrpc/success_response.hpp>
#include <mcp/jsonrpc/types.hpp>
