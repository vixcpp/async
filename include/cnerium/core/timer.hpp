#pragma once

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <map>
#include <utility>
#include <set>

#include <cnerium/core/task.hpp>
#include <cnerium/core/cancel.hpp>
#include <cnerium/core/error.hpp>

namespace cnerium::core
{
  class io_context;

  class timer
  {
  public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using duration = clock::duration;

    explicit timer(io_context &ctx);
    ~timer();
    timer(const timer &) = delete;
    timer &operator=(const timer &) = delete;

    // Fire-and-forget: call fn on the event loop after delay.
    template <typename Fn>
    void after(duration d, Fn &&fn, cancel_token ct = {})
    {
      schedule(clock::now() + d, make_job(std::forward<Fn>(fn)), std::move(ct));
    }

    // Coroutine-friendly sleep.
    task<void> sleep_for(duration d, cancel_token ct = {});
    // Stop timer thread and cancel pending timers.
    void stop() noexcept;

  private:
    struct job
    {
      virtual ~job() = default;
      virtual void run() = 0;
    };

    template <typename Fn>
    struct job_impl final : job
    {
      std::decay_t<Fn> fn;
      explicit job_impl(Fn &&f) : fn(std::forward<Fn>(f)) {}
      void run() override { fn(); }
    };

    template <typename Fn>
    static std::unique_ptr<job> make_job(Fn &&fn)
    {
      return std::unique_ptr<job>(new job_impl<Fn>(std::forward<Fn>(fn)));
    }

    void schedule(time_point tp, std::unique_ptr<job> j, cancel_token ct);
    void timer_loop();
    void ctx_post(std::function<void()> fn);

  private:
    io_context &ctx_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::uint64_t seq_{0};

    struct entry
    {
      time_point when;
      std::uint64_t id;
      cancel_token ct;
      std::unique_ptr<job> j;
    };

    struct cmp
    {
      bool operator()(const entry &a, const entry &b) const noexcept
      {
        if (a.when != b.when)
          return a.when < b.when;
        return a.id < b.id;
      }
    };

    std::multiset<entry, cmp> q_;
    bool stop_{false};
    std::thread worker_;
  };

} // namespace cnerium::core
