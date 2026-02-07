/**
 *
 *  @file timer.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_ASYNC_TIMER_HPP
#define VIX_ASYNC_TIMER_HPP

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <map>
#include <utility>
#include <set>
#include <thread>

#include <vix/async/core/task.hpp>
#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>

namespace vix::async::core
{
  class io_context;

  /**
   * @brief Lightweight timer service integrated with io_context.
   *
   * timer provides:
   * - after(): schedule a callback to run after a duration
   * - sleep_for(): coroutine-friendly delay (task<void>)
   *
   * Internally, it keeps an ordered queue of timer entries and uses a
   * worker thread to wait for the next deadline. When a timer fires, the
   * completion is posted back onto the io_context scheduler thread.
   *
   * Cancellation:
   * - A cancel_token can be provided per scheduled entry.
   * - If cancellation is observed before execution, the entry is skipped.
   */
  class timer
  {
  public:
    /**
     * @brief Clock type used by the timer.
     */
    using clock = std::chrono::steady_clock;

    /**
     * @brief Time point type for deadlines.
     */
    using time_point = clock::time_point;

    /**
     * @brief Duration type used by the timer.
     */
    using duration = clock::duration;

    /**
     * @brief Construct a timer service bound to an io_context.
     *
     * @param ctx Runtime context used to post callbacks and coroutine resumptions.
     */
    explicit timer(io_context &ctx);

    /**
     * @brief Destroy the timer service.
     *
     * Stops the worker thread and releases all queued jobs.
     */
    ~timer();

    /**
     * @brief timer is non-copyable.
     */
    timer(const timer &) = delete;

    /**
     * @brief timer is non-copyable.
     */
    timer &operator=(const timer &) = delete;

    /**
     * @brief Schedule a callable to run after the given duration.
     *
     * The callable is wrapped into a type-erased job and inserted into the
     * ordered timer queue.
     *
     * @tparam Fn Callable type.
     * @param d Delay duration.
     * @param fn Callable to execute.
     * @param ct Cancellation token.
     */
    template <typename Fn>
    void after(duration d, Fn &&fn, cancel_token ct = {})
    {
      schedule(clock::now() + d, make_job(std::forward<Fn>(fn)), std::move(ct));
    }

    /**
     * @brief Coroutine-friendly sleep for the given duration.
     *
     * Suspends the awaiting coroutine and resumes it after the duration elapses,
     * posting the continuation back onto the io_context scheduler thread.
     *
     * @param d Delay duration.
     * @param ct Cancellation token.
     * @return task<void> completing after the duration.
     */
    task<void> sleep_for(duration d, cancel_token ct = {});

    /**
     * @brief Stop the timer service.
     *
     * Wakes the worker thread and prevents further execution of queued jobs.
     */
    void stop() noexcept;

  private:
    /**
     * @brief Type-erased timer job.
     */
    struct job
    {
      virtual ~job() = default;

      /**
       * @brief Execute the job.
       */
      virtual void run() = 0;
    };

    /**
     * @brief Concrete timer job wrapper.
     *
     * @tparam Fn Callable type.
     */
    template <typename Fn>
    struct job_impl final : job
    {
      /**
       * @brief Stored callable.
       */
      std::decay_t<Fn> fn;

      /**
       * @brief Construct from a callable.
       *
       * @param f Callable to store.
       */
      explicit job_impl(Fn &&f) : fn(std::forward<Fn>(f)) {}

      /**
       * @brief Run the stored callable.
       */
      void run() override { fn(); }
    };

    /**
     * @brief Create a type-erased job from a callable.
     *
     * @tparam Fn Callable type.
     * @param fn Callable to wrap.
     * @return Unique pointer to the type-erased job.
     */
    template <typename Fn>
    static std::unique_ptr<job> make_job(Fn &&fn)
    {
      return std::unique_ptr<job>(new job_impl<Fn>(std::forward<Fn>(fn)));
    }

    /**
     * @brief Schedule an entry at a specific time point.
     *
     * @param tp Deadline time.
     * @param j Job to execute.
     * @param ct Cancellation token.
     */
    void schedule(time_point tp, std::unique_ptr<job> j, cancel_token ct);

    /**
     * @brief Worker loop waiting for the next deadline and dispatching jobs.
     */
    void timer_loop();

    /**
     * @brief Post a function onto the io_context scheduler.
     *
     * @param fn Function to execute on the scheduler thread.
     */
    void ctx_post(std::function<void()> fn);

  private:
    /**
     * @brief Bound runtime context.
     */
    io_context &ctx_;

    /**
     * @brief Mutex protecting timer queue and stop flag.
     */
    mutable std::mutex m_;

    /**
     * @brief Condition variable used by the worker to wait until the next deadline.
     */
    std::condition_variable cv_;

    /**
     * @brief Monotonic sequence used to break ties for identical deadlines.
     */
    std::uint64_t seq_{0};

    /**
     * @brief Scheduled entry stored in the ordered queue.
     */
    struct entry
    {
      /**
       * @brief Deadline time.
       */
      time_point when;

      /**
       * @brief Sequence identifier for strict ordering.
       */
      std::uint64_t id;

      /**
       * @brief Cancellation token bound to this entry.
       */
      cancel_token ct;

      /**
       * @brief Job to execute.
       */
      std::unique_ptr<job> j;
    };

    /**
     * @brief Comparator ordering entries by (when, id).
     */
    struct cmp
    {
      bool operator()(const entry &a, const entry &b) const noexcept
      {
        if (a.when != b.when)
          return a.when < b.when;
        return a.id < b.id;
      }
    };

    /**
     * @brief Ordered timer queue.
     */
    std::multiset<entry, cmp> q_;

    /**
     * @brief Stop request flag observed by the worker loop.
     */
    bool stop_{false};

    /**
     * @brief Worker thread responsible for sleeping until deadlines.
     */
    std::thread worker_;
  };

} // namespace vix::async::core

#endif // VIX_ASYNC_TIMER_HPP
