#pragma once

#include <memory>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

class Session;

namespace transport
{

class Transport
{
public:
  virtual ~Transport() = default;

  virtual auto attach(std::weak_ptr<Session> session) -> void = 0;
  virtual auto start() -> void = 0;
  virtual auto stop() -> void = 0;
  virtual auto isRunning() const noexcept -> bool = 0;
  virtual auto send(jsonrpc::Message message) -> void = 0;
};

}  // namespace transport
}  // namespace mcp
