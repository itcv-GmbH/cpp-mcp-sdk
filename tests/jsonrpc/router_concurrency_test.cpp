#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/error_reporter.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>

static constexpr std::int64_t kResponseWaitMillis = 500;
static constexpr std::int64_t kConcurrentRequestCount = 100;

static auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

class MessageCapture
{
public:
  auto record(mcp::jsonrpc::Message message) -> void
  {
    {
      const std::scoped_lock lock(mutex_);
      messages_.push_back(std::move(message));
    }
    messagesCv_.notify_all();
  }

  [[nodiscard]] auto waitForMessageCount(std::size_t expectedCount, std::chrono::milliseconds timeout) -> bool
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return messagesCv_.wait_for(lock, timeout, [&]() -> bool { return messages_.size() >= expectedCount; });
  }

  [[nodiscard]] auto messageCount() const -> std::size_t
  {
    const std::scoped_lock lock(mutex_);
    return messages_.size();
  }

  [[nodiscard]] auto copyMessages() const -> std::vector<mcp::jsonrpc::Message>
  {
    const std::scoped_lock lock(mutex_);
    return messages_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable messagesCv_;
  std::vector<mcp::jsonrpc::Message> messages_;
};

TEST_CASE("Concurrent sendRequest calls route responses correctly", "[jsonrpc][router][concurrency]")
{
  mcp::jsonrpc::Router router;
  MessageCapture outboundMessages;
  router.setOutboundMessageSender([&outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { outboundMessages.record(std::move(message)); });

  const mcp::jsonrpc::RequestContext context;
  std::vector<std::future<mcp::jsonrpc::Response>> futures;
  futures.reserve(kConcurrentRequestCount);

  // Issue many concurrent requests
  for (std::int64_t i = 0; i < kConcurrentRequestCount; ++i)
  {
    mcp::jsonrpc::Request request;
    request.id = i;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["index"] = i;

    futures.push_back(router.sendRequest(context, std::move(request)));
  }

  // Wait for all outbound messages to be captured
  REQUIRE(outboundMessages.waitForMessageCount(static_cast<std::size_t>(kConcurrentRequestCount), std::chrono::milliseconds(kResponseWaitMillis)));
  REQUIRE(outboundMessages.messageCount() == static_cast<std::size_t>(kConcurrentRequestCount));

  // Dispatch responses in REVERSE order to test correct routing
  for (std::int64_t i = kConcurrentRequestCount - 1; i >= 0; --i)
  {
    mcp::jsonrpc::SuccessResponse response;
    response.id = i;
    response.result = mcp::jsonrpc::JsonValue::object();
    response.result["receivedIndex"] = i;

    REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {response}));
  }

  // Verify all futures receive the correct response
  for (std::int64_t i = 0; i < kConcurrentRequestCount; ++i)
  {
    const std::future_status status = futures[static_cast<std::size_t>(i)].wait_for(std::chrono::milliseconds(kResponseWaitMillis));
    REQUIRE(status == std::future_status::ready);

    const mcp::jsonrpc::Response response = futures[static_cast<std::size_t>(i)].get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

    const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
    REQUIRE(success.id.index() == 0);  // std::int64_t variant
    REQUIRE(std::get<std::int64_t>(success.id) == i);
  }
}

TEST_CASE("Concurrent sendRequest calls from multiple threads", "[jsonrpc][router][concurrency]")
{
  mcp::jsonrpc::Router router;
  MessageCapture outboundMessages;
  router.setOutboundMessageSender([&outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { outboundMessages.record(std::move(message)); });

  const std::int64_t numThreads = 8;
  const std::int64_t requestsPerThread = 25;
  const std::int64_t totalRequests = numThreads * requestsPerThread;

  std::vector<std::thread> threads;
  std::vector<std::future<mcp::jsonrpc::Response>> allFutures;
  std::mutex futuresMutex;

  // Launch multiple threads, each issuing concurrent requests
  for (std::int64_t t = 0; t < numThreads; ++t)
  {
    threads.emplace_back(
      [&router, &allFutures, &futuresMutex, t, requestsPerThread]() -> void
      {
        mcp::jsonrpc::RequestContext context;
        context.sessionId = std::string("thread-") + std::to_string(t);

        for (std::int64_t r = 0; r < requestsPerThread; ++r)
        {
          const std::int64_t requestId = t * requestsPerThread + r;

          mcp::jsonrpc::Request request;
          request.id = requestId;
          request.method = "tools/call";
          request.params = mcp::jsonrpc::JsonValue::object();
          (*request.params)["thread"] = t;
          (*request.params)["request"] = r;

          auto future = router.sendRequest(context, std::move(request));
          {
            const std::scoped_lock lock(futuresMutex);
            allFutures.push_back(std::move(future));
          }
        }
      });
  }

  // Wait for all threads to finish sending
  for (auto &thread : threads)
  {
    thread.join();
  }

  // Wait for all outbound messages
  REQUIRE(outboundMessages.waitForMessageCount(static_cast<std::size_t>(totalRequests), std::chrono::milliseconds(kResponseWaitMillis)));

  // Dispatch responses from a different thread
  std::thread responseThread(
    [&router, totalRequests]() -> void
    {
      const mcp::jsonrpc::RequestContext context;

      // Send responses in scrambled order
      for (std::int64_t i = totalRequests - 1; i >= 0; --i)
      {
        mcp::jsonrpc::SuccessResponse response;
        response.id = i;
        response.result = mcp::jsonrpc::JsonValue::object();
        response.result["responseId"] = i;

        router.dispatchResponse(context, mcp::jsonrpc::Response {response});
      }
    });

  responseThread.join();

  // Verify all futures are resolved
  REQUIRE(allFutures.size() == static_cast<std::size_t>(totalRequests));

  std::atomic<std::size_t> successCount {0};
  for (auto &future : allFutures)
  {
    const std::future_status status = future.wait_for(std::chrono::milliseconds(kResponseWaitMillis));
    if (status == std::future_status::ready)
    {
      const mcp::jsonrpc::Response response = future.get();
      if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
      {
        successCount.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  REQUIRE(successCount.load() == static_cast<std::size_t>(totalRequests));
}

TEST_CASE("Router shutdown with in-flight requests completes without throwing", "[jsonrpc][router][concurrency][shutdown]")
{
  std::vector<mcp::ErrorEvent> capturedErrors;
  std::mutex errorsMutex;

  mcp::RouterOptions options;
  options.errorReporter = [&capturedErrors, &errorsMutex](const mcp::ErrorEvent &event) -> void
  {
    const std::scoped_lock lock(errorsMutex);
    capturedErrors.push_back(event);
  };

  auto router = std::make_unique<mcp::jsonrpc::Router>(options);

  // Set up a message sender that doesn't actually send (to keep requests in-flight)
  std::atomic<std::size_t> sentCount {0};
  router->setOutboundMessageSender([&sentCount](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message) -> void { sentCount.fetch_add(1, std::memory_order_relaxed); });

  const mcp::jsonrpc::RequestContext context;
  std::vector<std::future<mcp::jsonrpc::Response>> futures;
  futures.reserve(kConcurrentRequestCount);

  // Send many requests that will remain in-flight
  for (std::int64_t i = 0; i < kConcurrentRequestCount; ++i)
  {
    mcp::jsonrpc::Request request;
    request.id = i;
    request.method = "tools/call";
    request.params = mcp::jsonrpc::JsonValue::object();

    futures.push_back(router->sendRequest(context, std::move(request)));
  }

  // Verify requests were sent
  REQUIRE(sentCount.load() == static_cast<std::size_t>(kConcurrentRequestCount));

  // Shutdown should not throw
  REQUIRE_NOTHROW(
    [&]() -> void
    {
      std::future<void> destroyFuture = std::async(std::launch::async, [&router]() -> void { router.reset(); });

      REQUIRE(destroyFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
      destroyFuture.get();
    }());

  // All futures should be resolved (with error since router shut down)
  std::size_t resolvedCount = 0;
  for (auto &future : futures)
  {
    const std::future_status status = future.wait_for(std::chrono::milliseconds(100));
    if (status == std::future_status::ready)
    {
      resolvedCount++;
      // The response should be an error since the router shut down
      const mcp::jsonrpc::Response response = future.get();
      REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    }
  }

  // All futures should be resolved
  REQUIRE(resolvedCount == static_cast<std::size_t>(kConcurrentRequestCount));
}

TEST_CASE("Router shutdown during concurrent dispatchRequest completes all promises", "[jsonrpc][router][concurrency][shutdown]")
{
  std::vector<mcp::ErrorEvent> capturedErrors;
  std::mutex errorsMutex;

  mcp::RouterOptions options;
  options.errorReporter = [&capturedErrors, &errorsMutex](const mcp::ErrorEvent &event) -> void
  {
    const std::scoped_lock lock(errorsMutex);
    capturedErrors.push_back(event);
  };

  auto router = std::make_unique<mcp::jsonrpc::Router>(options);

  // Register a handler that blocks until we signal it
  std::atomic<std::size_t> handlerStartedCount {0};
  std::promise<void> releaseHandlers;
  auto releaseFuture = releaseHandlers.get_future();

  router->registerRequestHandler(
    "slow/op",
    [&handlerStartedCount, &releaseFuture](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
    {
      handlerStartedCount.fetch_add(1, std::memory_order_relaxed);

      return std::async(std::launch::async,
                        [&releaseFuture, request]() -> mcp::jsonrpc::Response
                        {
                          releaseFuture.wait();
                          mcp::jsonrpc::SuccessResponse success;
                          success.id = request.id;
                          success.result = mcp::jsonrpc::JsonValue::object();
                          return mcp::jsonrpc::Response {success};
                        });
    });

  const mcp::jsonrpc::RequestContext context;
  std::vector<std::future<mcp::jsonrpc::Response>> futures;
  futures.reserve(kConcurrentRequestCount);

  // Dispatch many requests concurrently
  for (std::int64_t i = 0; i < kConcurrentRequestCount; ++i)
  {
    mcp::jsonrpc::Request request;
    request.id = i;
    request.method = "slow/op";
    request.params = mcp::jsonrpc::JsonValue::object();

    futures.push_back(router->dispatchRequest(context, std::move(request)));
  }

  // Wait for handlers to start
  while (handlerStartedCount.load() < static_cast<std::size_t>(kConcurrentRequestCount))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Shutdown without releasing handlers - should not throw
  REQUIRE_NOTHROW(
    [&]() -> void
    {
      std::future<void> destroyFuture = std::async(std::launch::async, [&router]() -> void { router.reset(); });

      REQUIRE(destroyFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
      destroyFuture.get();
    }());

  // Now release the handlers (they should complete in the background)
  releaseHandlers.set_value();

  // All futures should be resolved (either successfully or with error)
  std::size_t resolvedCount = 0;
  for (auto &future : futures)
  {
    const std::future_status status = future.wait_for(std::chrono::milliseconds(kResponseWaitMillis));
    if (status == std::future_status::ready)
    {
      resolvedCount++;
      // Just verify we can get the result without throwing
      static_cast<void>(future.get());
    }
  }

  // All futures should be resolved
  REQUIRE(resolvedCount == static_cast<std::size_t>(kConcurrentRequestCount));
}

TEST_CASE("User callback exceptions are contained and reported", "[jsonrpc][router][concurrency][exceptions]")
{
  std::vector<mcp::ErrorEvent> capturedErrors;
  std::mutex errorsMutex;

  mcp::RouterOptions options;
  options.errorReporter = [&capturedErrors, &errorsMutex](const mcp::ErrorEvent &event) -> void
  {
    const std::scoped_lock lock(errorsMutex);
    capturedErrors.push_back(event);
  };

  mcp::jsonrpc::Router router(options);

  // Register a notification handler that throws
  router.registerNotificationHandler(
    "throwing/notification", [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &) -> void { throw std::runtime_error("Notification handler error"); });

  // Dispatch a notification - should not throw
  const mcp::jsonrpc::RequestContext context;
  mcp::jsonrpc::Notification notification;
  notification.method = "throwing/notification";
  notification.params = mcp::jsonrpc::JsonValue::object();

  REQUIRE_NOTHROW(router.dispatchNotification(context, notification));

  // Give error reporter time to be called
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify the error was reported
  {
    const std::scoped_lock lock(errorsMutex);
    REQUIRE(!capturedErrors.empty());
    bool foundExpectedError = false;
    for (const auto &error : capturedErrors)
    {
      if (error.message().find("Notification handler error") != std::string::npos)
      {
        foundExpectedError = true;
        break;
      }
    }
    REQUIRE(foundExpectedError);
  }
}

TEST_CASE("Progress callback exceptions are contained and reported", "[jsonrpc][router][concurrency][exceptions]")
{
  std::vector<mcp::ErrorEvent> capturedErrors;
  std::mutex errorsMutex;

  mcp::RouterOptions options;
  options.errorReporter = [&capturedErrors, &errorsMutex](const mcp::ErrorEvent &event) -> void
  {
    const std::scoped_lock lock(errorsMutex);
    capturedErrors.push_back(event);
  };

  mcp::jsonrpc::Router router(options);
  router.setOutboundMessageSender([](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message) -> void {});

  // Set up a request with a progress callback that throws
  const mcp::jsonrpc::RequestContext context;
  mcp::jsonrpc::Request request;
  request.id = std::int64_t {1};
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"]["progressToken"] = "progress-1";

  mcp::jsonrpc::OutboundRequestOptions requestOptions;
  requestOptions.onProgress = [&capturedErrors](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::ProgressUpdate &) -> void
  { throw std::runtime_error("Progress callback error"); };

  std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, request, std::move(requestOptions));

  // Send a progress notification - should not throw even though callback throws
  mcp::jsonrpc::Notification progressNotification;
  progressNotification.method = "notifications/progress";
  progressNotification.params = mcp::jsonrpc::JsonValue::object();
  (*progressNotification.params)["progressToken"] = "progress-1";
  (*progressNotification.params)["progress"] = 0.5;

  REQUIRE_NOTHROW(router.dispatchNotification(context, progressNotification));

  // Give error reporter time to be called
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify the error was reported
  {
    const std::scoped_lock lock(errorsMutex);
    bool foundExpectedError = false;
    for (const auto &error : capturedErrors)
    {
      if (error.message().find("Progress callback error") != std::string::npos)
      {
        foundExpectedError = true;
        break;
      }
    }
    REQUIRE(foundExpectedError);
  }

  // Complete the request
  mcp::jsonrpc::SuccessResponse response;
  response.id = std::int64_t {1};
  response.result = mcp::jsonrpc::JsonValue::object();
  router.dispatchResponse(context, mcp::jsonrpc::Response {response});

  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
}
