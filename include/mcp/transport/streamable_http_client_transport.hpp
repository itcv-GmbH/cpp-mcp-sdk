#pragma once

#include <functional>
#include <memory>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/transport.hpp>

namespace mcp::transport
{

auto makeStreamableHttpClientTransport(http::StreamableHttpClientOptions options,
                                       http::StreamableHttpClient::RequestExecutor requestExecutor,
                                       std::function<void(const jsonrpc::Message &)> inboundMessageHandler) -> std::shared_ptr<Transport>;

}  // namespace mcp::transport
