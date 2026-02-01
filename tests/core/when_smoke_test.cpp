#include <cassert>
#include <chrono>
#include <iostream>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cnerium/core/scheduler.hpp>
#include <cnerium/core/task.hpp>
#include <cnerium/core/when.hpp>

using cnerium::core::scheduler;
using cnerium::core::task;
using cnerium::core::when_all;
using cnerium::core::when_any;

// ------------------------------------------------------------
// Helpers: drive a scheduler loop while awaiting a task
// ------------------------------------------------------------

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

template <typename T>
static T sync_await(task<T> t)
{
  struct runner
  {
    task<T> inner;
    std::optional<T> value;

    task<void> run()
    {
      value = co_await inner;
      co_return;
    }
  };

  runner r{std::move(t), std::nullopt};
  auto top = r.run();
  auto h = top.handle();
  assert(h);

  while (!h.done())
    h.resume();

  assert(r.value.has_value());
  return std::move(*r.value);
}

static void sync_await(task<void> t)
{
  struct runner
  {
    task<void> inner;

    task<void> run()
    {
      co_await inner;
      co_return;
    }
  };

  runner r{std::move(t)};
  auto top = r.run();
  auto h = top.handle();
  assert(h);

  while (!h.done())
    h.resume();
}

// ------------------------------------------------------------
// when_any compatibility helpers:
// support tuple<optional<T>...> and tuple<T...>
// ------------------------------------------------------------

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

// ------------------------------------------------------------
// Test coroutines
// ------------------------------------------------------------

static task<int> immediate(int v)
{
  co_return v;
}

static task<int> delayed_value(scheduler &sched, int v, int delay_ms)
{
  // Force execution onto the scheduler thread first
  co_await sched.schedule();

  struct awaitable
  {
    scheduler *s{};
    int ms{0};

    bool await_ready() const noexcept { return ms <= 0; }

    void await_suspend(std::coroutine_handle<> h)
    {
      std::thread([s = s, ms = ms, h]() mutable
                  {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                    s->post(h); })
          .detach();
    }

    void await_resume() noexcept {}
  };

  co_await awaitable{&sched, delay_ms};
  co_return v;
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

static task<void> test_when_all_mixed_timing(scheduler &sched)
{
  co_await sched.schedule();

  auto tup = co_await when_all(
      sched,
      delayed_value(sched, 1, 50),
      delayed_value(sched, 2, 10),
      delayed_value(sched, 3, 30));

  static_assert(std::tuple_size_v<decltype(tup)> == 3);
  assert(std::get<0>(tup) == 1);
  assert(std::get<1>(tup) == 2);
  assert(std::get<2>(tup) == 3);

  co_return;
}

static task<void> test_when_any_picks_first(scheduler &sched)
{
  co_await sched.schedule();

  auto result = co_await when_any(
      sched,
      delayed_value(sched, 111, 60),
      delayed_value(sched, 222, 10));

  const std::size_t idx = result.first;
  const auto &vals = result.second;

  assert(idx == 1);

  const auto &v1 = std::get<1>(vals);
  assert(is_ready(v1));
  assert(get_value(v1) == 222);

  co_return;
}

static task<void> test_when_any_handles_immediate(scheduler &sched)
{
  co_await sched.schedule();

  auto result = co_await when_any(
      sched,
      immediate(7),
      delayed_value(sched, 9, 30));

  const std::size_t idx = result.first;
  const auto &vals = result.second;

  assert(idx == 0);

  const auto &v0 = std::get<0>(vals);
  assert(is_ready(v0));
  assert(get_value(v0) == 7);

  co_return;
}

int main()
{
  scheduler sched;
  scheduler_runner run{sched};

  sync_await(test_when_all_basic(sched));
  sync_await(test_when_all_mixed_timing(sched));
  sync_await(test_when_any_picks_first(sched));
  sync_await(test_when_any_handles_immediate(sched));

  std::cout << "cnerium_when_smoke: OK\n";
  return 0;
}
