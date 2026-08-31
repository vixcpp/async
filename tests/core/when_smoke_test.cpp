/**
 *
 *  @file when_smoke_test.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#include <vix/async/core/scheduler.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/core/when.hpp>

using vix::async::core::scheduler;
using vix::async::core::task;
using vix::async::core::when_all;
using vix::async::core::when_any;

// Helpers: run scheduler loop in background
struct scheduler_runner
{
  scheduler &sched;
  std::thread t;

  explicit scheduler_runner(scheduler &s)
      : sched(s),
        t([this]()
          { sched.run(); })
  {
  }

  ~scheduler_runner()
  {
    sched.stop();
    if (t.joinable())
      t.join();
  }
};

// sync_wait for task<T> that is scheduler-driven (no manual resume!)
template <typename T>
static T sync_wait(scheduler &sched, task<T> t)
{
  auto p = std::make_shared<std::promise<T>>();
  auto fut = p->get_future();

  auto wrapper = [p, inner = std::move(t)]() mutable -> task<void>
  {
    try
    {
      if constexpr (std::is_void_v<T>)
      {
        co_await inner;
        p->set_value();
      }
      else
      {
        T v = co_await inner;
        p->set_value(std::move(v));
      }
    }
    catch (...)
    {
      p->set_exception(std::current_exception());
    }
    co_return;
  };

  // start wrapper on the scheduler thread
  std::move(wrapper()).start(sched);

  if constexpr (std::is_void_v<T>)
  {
    fut.get();
    return;
  }
  else
  {
    return fut.get();
  }
}

// when_any compatibility helpers
template <typename X>
static bool is_ready(const X &x)
{
  if constexpr (requires { x.has_value(); })
    return x.has_value(); // optional<T>
  else
    return true; // T direct
}

template <typename X>
static decltype(auto) get_value(X &&x)
{
  if constexpr (requires { *x; })
    return *std::forward<X>(x); // optional<T>
  else
    return std::forward<X>(x); // T direct
}

// Test-only lifetime tracker for synthetic delayed completions.
// when_any intentionally leaves losing tasks running, so the test keeps the
// scheduler alive until every detached delay source has posted its completion.
struct delayed_jobs
{
  void reserve()
  {
    std::lock_guard<std::mutex> lock(m);
    ++outstanding;
  }

  void complete()
  {
    std::lock_guard<std::mutex> lock(m);
    assert(outstanding > 0);
    --outstanding;
    cv.notify_all();
  }

  void wait()
  {
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [this]()
            { return outstanding == 0; });
  }

  std::mutex m;
  std::condition_variable cv;
  std::size_t outstanding{0};
};

static task<int> immediate(int v)
{
  co_return v;
}

static task<int> delayed_value_impl(
    scheduler &sched,
    delayed_jobs &jobs,
    int v,
    int delay_ms)
{
  // Force execution onto the scheduler thread first.
  co_await sched.schedule();

  struct awaitable
  {
    scheduler *s{};
    delayed_jobs *jobs{};
    int ms{0};

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
      std::thread([sched = s, tracked = jobs, delay_ms = ms, h]() mutable
                  {
                    if (delay_ms > 0)
                    {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds(delay_ms));
                    }

                    sched->post(h);
                    tracked->complete(); })
          .detach();
    }

    void await_resume() noexcept {}
  };

  co_await awaitable{&sched, &jobs, delay_ms};
  co_return v;
}

static task<int> delayed_value(
    scheduler &sched,
    delayed_jobs &jobs,
    int v,
    int delay_ms)
{
  // Reserve synchronously, before the lazy coroutine is returned. This makes
  // delayed_jobs::wait() a real lifetime barrier even if a when_any winner
  // completes before this coroutine reaches await_suspend().
  jobs.reserve();
  return delayed_value_impl(sched, jobs, v, delay_ms);
}

static task<void> test_when_all_basic(scheduler &sched)
{
  co_await sched.schedule();

  auto tup = co_await when_all(sched, immediate(10), immediate(20));
  static_assert(std::tuple_size_v<decltype(tup)> == 2);

  assert(std::get<0>(tup) == 10);
  assert(std::get<1>(tup) == 20);

  co_return;
}

static task<void> test_when_all_empty(scheduler &sched)
{
  auto tup = co_await when_all(sched);
  static_assert(std::tuple_size_v<decltype(tup)> == 0);
  co_return;
}

static task<void> test_when_all_mixed_timing(scheduler &sched, delayed_jobs &jobs)
{
  co_await sched.schedule();

  auto tup = co_await when_all(
      sched,
      delayed_value(sched, jobs, 1, 50),
      delayed_value(sched, jobs, 2, 10),
      delayed_value(sched, jobs, 3, 30));

  static_assert(std::tuple_size_v<decltype(tup)> == 3);
  assert(std::get<0>(tup) == 1);
  assert(std::get<1>(tup) == 2);
  assert(std::get<2>(tup) == 3);

  co_return;
}

static task<void> test_when_any_picks_first(scheduler &sched, delayed_jobs &jobs)
{
  co_await sched.schedule();

  auto result = co_await when_any(
      sched,
      delayed_value(sched, jobs, 111, 60),
      delayed_value(sched, jobs, 222, 10));

  const auto &vals = result.second;

  if (result.first != 1)
    throw std::runtime_error("when_any: expected index 1");

  const auto &v1 = std::get<1>(vals);
  if (!is_ready(v1) || get_value(v1) != 222)
    throw std::runtime_error("when_any: wrong value");

  co_return;
}

static task<void> test_when_any_handles_immediate(scheduler &sched, delayed_jobs &jobs)
{
  co_await sched.schedule();

  auto result = co_await when_any(
      sched,
      immediate(7),
      delayed_value(sched, jobs, 9, 30));

  const auto &vals = result.second;

  if (result.first != 0)
    throw std::runtime_error("when_any: expected index 0");

  const auto &v0 = std::get<0>(vals);
  if (!is_ready(v0) || get_value(v0) != 7)
    throw std::runtime_error("when_any: wrong value");

  co_return;
}

int main()
{
  delayed_jobs jobs;
  scheduler sched;
  scheduler_runner run{sched};

  sync_wait<void>(sched, test_when_all_basic(sched));
  sync_wait<void>(sched, test_when_all_empty(sched));
  sync_wait<void>(sched, test_when_all_mixed_timing(sched, jobs));
  sync_wait<void>(sched, test_when_any_picks_first(sched, jobs));
  sync_wait<void>(sched, test_when_any_handles_immediate(sched, jobs));

  // when_any returns when the winner completes. Losing tasks keep running.
  // Wait for every synthetic external completion source before allowing the
  // scheduler to be destroyed.
  jobs.wait();

  std::cout << "async_when_smoke: OK\n";
  return 0;
}
