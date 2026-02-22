#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/jsonrpc/router.hpp>

namespace
{

constexpr auto kWaitTimeout = std::chrono::seconds {2};
constexpr std::size_t kThreadCount = 8;
constexpr std::size_t kRequestsPerThread = 16;
constexpr std::size_t kTotalRequests = kThreadCount * kRequestsPerThread;

struct ConcurrentFailureCollector
{
  auto record(std::string failure) -> void
  {
    const std::scoped_lock lock(mutex);
    failures.push_back(std::move(failure));
  }

  [[nodiscard]] auto snapshot() const -> std::vector<std::string>
  {
    const std::scoped_lock lock(mutex);
    return failures;
  }

  mutable std::mutex mutex;
  std::vector<std::string> failures;
};

}  // namespace

TEST_CASE("Router deterministically routes responses for concurrent senders", "[concurrency][routing]")
{
  mcp::jsonrpc::Router router;

  std::vector<std::future<mcp::jsonrpc::Response>> responseFutures(kTotalRequests);

  std::promise<void> allOutboundPromise;
  std::future<void> allOutboundFuture = allOutboundPromise.get_future();
  std::once_flag allOutboundOnce;
  std::atomic<std::size_t> outboundCount {0};
  ConcurrentFailureCollector failureCollector;

  router.setOutboundMessageSender(
    [&allOutboundPromise, &allOutboundOnce, &outboundCount, &failureCollector](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
    {
      if (!std::holds_alternative<mcp::jsonrpc::Request>(message))
      {
        failureCollector.record("Outbound sender received non-request message.");
      }
      else
      {
        const auto &request = std::get<mcp::jsonrpc::Request>(message);
        if (!std::holds_alternative<std::int64_t>(request.id))
        {
          failureCollector.record("Outbound sender received request with non-int64 id.");
        }
      }

      const std::size_t sent = outboundCount.fetch_add(1, std::memory_order_relaxed) + 1;
      if (sent == kTotalRequests)
      {
        std::call_once(allOutboundOnce, [&allOutboundPromise]() -> void { allOutboundPromise.set_value(); });
      }
    });

  std::promise<void> releaseWorkersPromise;
  std::shared_future<void> releaseWorkers = releaseWorkersPromise.get_future().share();

  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);

  for (std::size_t threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
  {
    workers.emplace_back(
      [&router, &releaseWorkers, &responseFutures, &failureCollector, threadIndex]() -> void
      {
        if (releaseWorkers.wait_for(kWaitTimeout) != std::future_status::ready)
        {
          failureCollector.record("Worker timed out waiting for release barrier.");
          return;
        }

        mcp::jsonrpc::RequestContext context;
        context.sessionId = std::string("sender-") + std::to_string(threadIndex);

        for (std::size_t localRequestIndex = 0; localRequestIndex < kRequestsPerThread; ++localRequestIndex)
        {
          const std::size_t globalRequestIndex = (threadIndex * kRequestsPerThread) + localRequestIndex;
          const auto requestId = static_cast<std::int64_t>(globalRequestIndex);

          mcp::jsonrpc::Request request;
          request.id = requestId;
          request.method = "tools/call";
          request.params = mcp::jsonrpc::JsonValue::object();
          (*request.params)["threadIndex"] = static_cast<std::int64_t>(threadIndex);
          (*request.params)["localRequestIndex"] = static_cast<std::int64_t>(localRequestIndex);

          responseFutures[globalRequestIndex] = router.sendRequest(context, std::move(request));
        }
      });
  }

  releaseWorkersPromise.set_value();

  for (auto &worker : workers)
  {
    worker.join();
  }

  REQUIRE(allOutboundFuture.wait_for(kWaitTimeout) == std::future_status::ready);
  allOutboundFuture.get();
  REQUIRE(outboundCount.load(std::memory_order_relaxed) == kTotalRequests);

  const std::vector<std::string> concurrentFailures = failureCollector.snapshot();
  for (const std::string &failure : concurrentFailures)
  {
    INFO(failure);
  }
  REQUIRE(concurrentFailures.empty());

  // Resolve responses in deterministic scrambled order (reverse, split by parity)
  const mcp::jsonrpc::RequestContext responseContext;
  for (std::size_t parity = 0; parity < 2; ++parity)
  {
    for (std::size_t offset = 0; offset < kTotalRequests; ++offset)
    {
      const std::size_t requestIndex = (kTotalRequests - 1) - offset;
      if ((requestIndex % 2) != parity)
      {
        continue;
      }

      mcp::jsonrpc::SuccessResponse response;
      response.id = static_cast<std::int64_t>(requestIndex);
      response.result = mcp::jsonrpc::JsonValue::object();
      response.result["requestIndex"] = static_cast<std::int64_t>(requestIndex);

      REQUIRE(router.dispatchResponse(responseContext, mcp::jsonrpc::Response {std::move(response)}));
    }
  }

  for (std::size_t requestIndex = 0; requestIndex < kTotalRequests; ++requestIndex)
  {
    REQUIRE(responseFutures[requestIndex].valid());
    REQUIRE(responseFutures[requestIndex].wait_for(kWaitTimeout) == std::future_status::ready);

    const mcp::jsonrpc::Response response = responseFutures[requestIndex].get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

    const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
    REQUIRE(std::holds_alternative<std::int64_t>(success.id));
    REQUIRE(std::get<std::int64_t>(success.id) == static_cast<std::int64_t>(requestIndex));
    REQUIRE(success.result["requestIndex"].as<std::int64_t>() == static_cast<std::int64_t>(requestIndex));
  }
}
