#include <catch2/catch_test_macros.hpp>
#include <mcp/security/limits.hpp>

TEST_CASE("Security limits defaults", "[security][limits]")
{
  SECTION("RuntimeLimits defaults match kDefault* constants")
  {
    mcp::security::RuntimeLimits limits;

    // Message size limits
    REQUIRE(limits.maxMessageSizeBytes == mcp::security::kDefaultMaxMessageSizeBytes);
    REQUIRE(limits.maxConcurrentInFlightRequests == mcp::security::kDefaultMaxConcurrentInFlightRequests);
    REQUIRE(limits.maxSseBufferedMessages == mcp::security::kDefaultMaxSseBufferedMessages);

    // SSE stream duration
    REQUIRE(limits.maxSseStreamDuration.count() == mcp::security::kDefaultMaxSseStreamDurationMilliseconds);

    // Retry limits
    REQUIRE(limits.maxRetryAttempts == mcp::security::kDefaultMaxRetryAttempts);
    REQUIRE(limits.maxRetryDelayMilliseconds == mcp::security::kDefaultMaxRetryDelayMilliseconds);

    // Task limits
    REQUIRE(limits.maxTaskTtlMilliseconds == mcp::security::kDefaultMaxTaskTtlMilliseconds);
    REQUIRE(limits.maxConcurrentTasksPerAuthContext == mcp::security::kDefaultMaxConcurrentTasksPerAuthContext);
  }

  SECTION("Default retry/task TTL limits are sane (non-zero, expected ranges)")
  {
    mcp::security::RuntimeLimits limits;

    // All limits should be non-zero
    REQUIRE(limits.maxMessageSizeBytes > 0);
    REQUIRE(limits.maxConcurrentInFlightRequests > 0);
    REQUIRE(limits.maxSseBufferedMessages > 0);
    REQUIRE(limits.maxSseStreamDuration.count() > 0);
    REQUIRE(limits.maxRetryAttempts > 0);
    REQUIRE(limits.maxRetryDelayMilliseconds > 0);
    REQUIRE(limits.maxTaskTtlMilliseconds > 0);
    REQUIRE(limits.maxConcurrentTasksPerAuthContext > 0);

    // Message size should be at least 1MB
    REQUIRE(limits.maxMessageSizeBytes >= 1024 * 1024);

    // Concurrent requests should be reasonable (>= 100)
    REQUIRE(limits.maxConcurrentInFlightRequests >= 100);

    // SSE buffered messages should be reasonable (>= 100)
    REQUIRE(limits.maxSseBufferedMessages >= 100);

    // SSE stream duration should be at least 1 minute (60000ms)
    REQUIRE(limits.maxSseStreamDuration.count() >= 60000);

    // Retry attempts should be reasonable (>= 10)
    REQUIRE(limits.maxRetryAttempts >= 10);

    // Retry delay should be at least 1 second (1000ms)
    REQUIRE(limits.maxRetryDelayMilliseconds >= 1000);

    // Task TTL should be at least 1 hour (3600000ms)
    REQUIRE(limits.maxTaskTtlMilliseconds >= 3600000);

    // Concurrent tasks per auth context should be reasonable (>= 10)
    REQUIRE(limits.maxConcurrentTasksPerAuthContext >= 10);
  }
}
