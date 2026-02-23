#pragma once

#include <cstdint>



namespace mcp::lifecycle::session
{

/**
 * @brief States in the session lifecycle.
 */
enum class SessionState : std::uint8_t
{
  kCreated,  ///< Session created, not yet initialized.
  kInitializing,  ///< Initialize request sent/received, waiting for response.
  kInitialized,  ///< Initialize response received/sent, waiting for initialized notification.
  kOperating,  ///< Full operation mode.
  kStopping,  ///< Shutdown in progress.
  kStopped,  ///< Session stopped.
};

} // namespace mcp::lifecycle::session


