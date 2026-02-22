#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/transport/http.hpp>

using namespace mcp;
using namespace mcp::transport;

TEST_CASE("HttpServerRuntime start() is idempotent", "[transport][threading]")
{
  HttpServerOptions options;
  options.endpoint.port = 0;  // Let OS assign port
  HttpServerRuntime runtime(options);

  // First start should succeed
  REQUIRE_NOTHROW(runtime.start());
  REQUIRE(runtime.isRunning());
  REQUIRE(runtime.localPort() != 0);

  // Second start should be idempotent (no-op)
  REQUIRE_NOTHROW(runtime.start());
  REQUIRE(runtime.isRunning());
  REQUIRE(runtime.localPort() != 0);

  // Third start should still be idempotent
  REQUIRE_NOTHROW(runtime.start());
  REQUIRE(runtime.isRunning());

  runtime.stop();
  REQUIRE(!runtime.isRunning());
}

TEST_CASE("HttpServerRuntime stop() is idempotent and noexcept", "[transport][threading]")
{
  HttpServerOptions options;
  options.endpoint.port = 0;
  HttpServerRuntime runtime(options);

  // Stop before start should be idempotent (no-op) and not throw
  REQUIRE_NOTHROW(runtime.stop());
  REQUIRE(!runtime.isRunning());

  runtime.start();
  REQUIRE(runtime.isRunning());

  // First stop should succeed
  REQUIRE_NOTHROW(runtime.stop());
  REQUIRE(!runtime.isRunning());

  // Second stop should be idempotent
  REQUIRE_NOTHROW(runtime.stop());
  REQUIRE(!runtime.isRunning());

  // Multiple stops should all be safe
  REQUIRE_NOTHROW(runtime.stop());
  REQUIRE_NOTHROW(runtime.stop());
}

TEST_CASE("HttpServerRuntime can be started and stopped repeatedly", "[transport][threading]")
{
  HttpServerOptions options;
  options.endpoint.port = 0;
  HttpServerRuntime runtime(options);

  // Multiple start/stop cycles
  for (int i = 0; i < 3; ++i)
  {
    REQUIRE_NOTHROW(runtime.start());
    REQUIRE(runtime.isRunning());
    REQUIRE(runtime.localPort() != 0);

    REQUIRE_NOTHROW(runtime.stop());
    REQUIRE(!runtime.isRunning());
  }
}

TEST_CASE("HttpServerRuntime destructor calls stop() noexcept", "[transport][threading]")
{
  std::uint16_t port = 0;

  {
    HttpServerOptions options;
    options.endpoint.port = 0;
    HttpServerRuntime runtime(options);

    runtime.start();
    port = runtime.localPort();
    REQUIRE(port != 0);

    // Destructor should call stop() and be noexcept
    // No REQUIRE needed - if it throws, the test fails
  }

  // Verify port is released by binding another server to it
  {
    HttpServerOptions options;
    options.endpoint.port = port;
    HttpServerRuntime runtime(options);

    // This should succeed if the port was properly released
    REQUIRE_NOTHROW(runtime.start());
    runtime.stop();
  }
}

TEST_CASE("StreamableHttpServerRunner start() is idempotent", "[runner][threading]")
{
  auto serverFactory = []() -> std::shared_ptr<Server> { return Server::create(); };

  StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.port = 0;

  StreamableHttpServerRunner runner(serverFactory, options);

  // First start
  REQUIRE_NOTHROW(runner.start());
  REQUIRE(runner.isRunning());
  REQUIRE(runner.localPort() != 0);

  // Second start should be idempotent
  REQUIRE_NOTHROW(runner.start());
  REQUIRE(runner.isRunning());

  runner.stop();
}

TEST_CASE("StreamableHttpServerRunner stop() is idempotent and noexcept", "[runner][threading]")
{
  auto serverFactory = []() -> std::shared_ptr<Server> { return Server::create(); };

  StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.port = 0;

  StreamableHttpServerRunner runner(serverFactory, options);

  // Stop before start should be safe
  REQUIRE_NOTHROW(runner.stop());
  REQUIRE(!runner.isRunning());

  runner.start();
  REQUIRE(runner.isRunning());

  // Multiple stops should all be safe
  REQUIRE_NOTHROW(runner.stop());
  REQUIRE(!runner.isRunning());

  REQUIRE_NOTHROW(runner.stop());
  REQUIRE_NOTHROW(runner.stop());
}

TEST_CASE("StreamableHttpServerRunner destructor stops server noexcept", "[runner][threading]")
{
  std::uint16_t port = 0;

  {
    auto serverFactory = []() -> std::shared_ptr<Server> { return Server::create(); };

    StreamableHttpServerRunnerOptions options;
    options.transportOptions.http.endpoint.port = 0;

    StreamableHttpServerRunner runner(serverFactory, options);

    runner.start();
    port = runner.localPort();
    REQUIRE(port != 0);

    // Destructor should stop the server and be noexcept
  }

  // Verify port was released
  {
    auto serverFactory = []() -> std::shared_ptr<Server> { return Server::create(); };

    StreamableHttpServerRunnerOptions options;
    options.transportOptions.http.endpoint.port = port;

    StreamableHttpServerRunner runner(serverFactory, options);
    REQUIRE_NOTHROW(runner.start());
    runner.stop();
  }
}

TEST_CASE("HttpServerRuntime background thread has noexcept entrypoint", "[transport][threading]")
{
  // This test verifies that the server thread doesn't allow exceptions to escape
  std::atomic<bool> errorReported {false};
  std::string errorMessage;

  HttpServerOptions options;
  options.endpoint.port = 0;
  options.errorReporter = [&errorReported, &errorMessage](const ErrorEvent &event)
  {
    errorReported.store(true);
    errorMessage = std::string(event.message());
  };

  HttpServerRuntime runtime(options);

  REQUIRE_NOTHROW(runtime.start());
  REQUIRE(runtime.isRunning());

  // Let the server run briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Stop should complete without throwing
  REQUIRE_NOTHROW(runtime.stop());
  REQUIRE(!runtime.isRunning());

  // No errors should have been reported during normal operation
  REQUIRE(!errorReported.load());
}

TEST_CASE("HttpServerRuntime localPort() is thread-safe", "[transport][threading]")
{
  HttpServerOptions options;
  options.endpoint.port = 0;
  HttpServerRuntime runtime(options);

  runtime.start();

  std::vector<std::thread> threads;
  std::vector<std::uint16_t> ports(10);

  for (size_t i = 0; i < 10; ++i)
  {
    threads.emplace_back([&runtime, &ports, i]() { ports[i] = runtime.localPort(); });
  }

  for (auto &t : threads)
  {
    t.join();
  }

  // All threads should have gotten the same port
  for (size_t i = 1; i < 10; ++i)
  {
    REQUIRE(ports[i] == ports[0]);
    REQUIRE(ports[i] != 0);
  }

  runtime.stop();
}

TEST_CASE("HttpServerRuntime isRunning() is thread-safe", "[transport][threading]")
{
  HttpServerOptions options;
  options.endpoint.port = 0;
  HttpServerRuntime runtime(options);

  std::atomic<int> runningCount {0};
  std::atomic<int> notRunningCount {0};

  std::vector<std::thread> threads;

  // Create threads that check isRunning() concurrently
  for (size_t i = 0; i < 20; ++i)
  {
    threads.emplace_back(
      [&runtime, &runningCount, &notRunningCount]()
      {
        for (int j = 0; j < 100; ++j)
        {
          if (runtime.isRunning())
          {
            runningCount++;
          }
          else
          {
            notRunningCount++;
          }
          std::this_thread::yield();
        }
      });
  }

  // Start the server while threads are checking
  runtime.start();

  for (auto &t : threads)
  {
    t.join();
  }

  runtime.stop();

  // At least some checks should have seen running=true after start
  REQUIRE(runningCount.load() > 0);
}
