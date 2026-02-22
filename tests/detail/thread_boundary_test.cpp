#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/error_reporter.hpp>

// Include the thread_boundary implementation directly since it's in src/
#include "../../src/detail/thread_boundary.hpp"

// NOLINTBEGIN(readability-function-cognitive-complexity, misc-const-correctness, misc-include-cleaner)

using mcp::ErrorEvent;
using mcp::ErrorReporter;
using mcp::detail::threadBoundary;
using mcp::detail::threadPoolWork;

struct ThreadBoundaryTestFixture
{
  mutable std::mutex mutex;
  std::vector<std::pair<std::string, std::string>> recordedErrors;
  ErrorReporter errorReporter;

  ThreadBoundaryTestFixture()
  {
    errorReporter = [this](const ErrorEvent &event)
    {
      const std::scoped_lock lock(mutex);
      recordedErrors.push_back({std::string(event.component()), std::string(event.message())});
    };
  }

  auto getRecordedErrors() const -> std::vector<std::pair<std::string, std::string>>
  {
    const std::scoped_lock lock(mutex);
    return recordedErrors;
  }

  auto waitForErrors(std::size_t expectedCount, std::chrono::milliseconds timeout = std::chrono::milliseconds {100}) const -> bool
  {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout)
    {
      {
        const std::scoped_lock lock(mutex);
        if (recordedErrors.size() >= expectedCount)
        {
          return true;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    return false;
  }
};

TEST_CASE_METHOD(ThreadBoundaryTestFixture, "ThreadBoundary catches exception and reports", "[thread_boundary]")
{
  bool ran = false;
  auto throwingCallable = [&ran]()
  {
    ran = true;
    throw std::runtime_error("Test exception");
  };

  auto wrapped = threadBoundary(throwingCallable, errorReporter, "TestComponent");
  std::thread thread([wrapped]() noexcept { wrapped(); });
  thread.join();

  REQUIRE(ran);
  REQUIRE(waitForErrors(1));

  const auto errors = getRecordedErrors();
  REQUIRE(errors.size() == 1);
  REQUIRE(errors[0].first == "TestComponent");
  REQUIRE(errors[0].second.find("Test exception") != std::string::npos);
}

TEST_CASE_METHOD(ThreadBoundaryTestFixture, "ThreadBoundary with no exception reports nothing", "[thread_boundary]")
{
  bool ran = false;
  auto normalCallable = [&ran]() { ran = true; };

  auto wrapped = threadBoundary(normalCallable, errorReporter, "TestComponent");
  std::thread thread([wrapped]() noexcept { wrapped(); });
  thread.join();

  REQUIRE(ran);
  REQUIRE(getRecordedErrors().empty());
}

TEST_CASE_METHOD(ThreadBoundaryTestFixture, "ThreadBoundary with no error reporter silently catches", "[thread_boundary]")
{
  bool ran = false;
  auto throwingCallable = [&ran]()
  {
    ran = true;
    throw std::runtime_error("Test exception");
  };

  // Pass empty error reporter
  auto wrapped = threadBoundary(throwingCallable, ErrorReporter {}, "TestComponent");
  std::thread thread([wrapped]() noexcept { wrapped(); });
  thread.join();

  REQUIRE(ran);
  // No error reporter, so no errors should be recorded
  REQUIRE(getRecordedErrors().empty());
}

TEST_CASE_METHOD(ThreadBoundaryTestFixture, "ThreadPoolWork catches exception", "[thread_boundary]")
{
  bool ran = false;
  auto throwingCallable = [&ran]()
  {
    ran = true;
    throw std::logic_error("Logic error in thread pool");
  };

  auto wrapped = threadPoolWork(throwingCallable, errorReporter, "ThreadPoolComponent");
  wrapped();  // Direct invocation

  REQUIRE(ran);

  const auto errors = getRecordedErrors();
  REQUIRE(errors.size() == 1);
  REQUIRE(errors[0].first == "ThreadPoolComponent");
  REQUIRE(errors[0].second.find("Logic error") != std::string::npos);
}

TEST_CASE_METHOD(ThreadBoundaryTestFixture, "ThreadBoundary catches unknown exception types", "[thread_boundary]")
{
  bool ran = false;
  auto throwingCallable = [&ran]()
  {
    ran = true;
    throw 42;  // Throw non-exception type
  };

  auto wrapped = threadBoundary(throwingCallable, errorReporter, "UnknownExceptionTest");
  std::thread thread([wrapped]() noexcept { wrapped(); });
  thread.join();

  REQUIRE(ran);
  REQUIRE(waitForErrors(1));

  const auto errors = getRecordedErrors();
  REQUIRE(errors.size() == 1);
  REQUIRE(errors[0].first == "UnknownExceptionTest");
  REQUIRE(errors[0].second == "Unknown exception");
}

TEST_CASE_METHOD(ThreadBoundaryTestFixture, "Multiple thread boundaries report independently", "[thread_boundary]")
{
  constexpr int kNumThreads = 5;
  std::vector<std::thread> threads;

  for (int i = 0; i < kNumThreads; ++i)
  {
    auto throwingCallable = [i]() { throw std::runtime_error("Exception from thread " + std::to_string(i)); };

    auto wrapped = threadBoundary(throwingCallable, errorReporter, "MultiThreadTest");
    threads.emplace_back([wrapped]() noexcept { wrapped(); });
  }

  for (auto &thread : threads)
  {
    thread.join();
  }

  REQUIRE(waitForErrors(kNumThreads));
  REQUIRE(getRecordedErrors().size() == kNumThreads);
}

// NOLINTEND(readability-function-cognitive-complexity, misc-const-correctness, misc-include-cleaner)
