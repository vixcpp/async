/**
 *
 *  @file scheduler.hpp
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
#ifndef VIX_ASYNC_SCHEDULER_HPP
#define VIX_ASYNC_SCHEDULER_HPP

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace vix::async::core
{
  /**
   * @brief Minimal single-thread scheduler for tasks and coroutine resumption.
   *
   * scheduler provides a thread-safe queue of work and an event loop (run())
   * that executes enqueued work on the calling thread.
   *
   * Optimized design:
   * - coroutine handles use a dedicated fast path queue
   * - generic callables use a secondary queue
   * - no polymorphic type-erasure with manual new/delete
   * - run() prioritizes coroutine resumption to reduce async overhead
   *
   * Supported work items:
   * - Generic callables posted via post(Fn)
   * - Coroutine continuations posted via post(coroutine_handle)
   * - An awaitable (schedule()) to hop onto the scheduler thread from a coroutine
   *
   * This is a small building block designed to be embedded into higher-level
   * runtime contexts (e.g. io_context).
   */
  class scheduler
  {
  public:
    /**
     * @brief Construct a scheduler.
     */
    scheduler() = default;

    /**
     * @brief scheduler is non-copyable.
     */
    scheduler(const scheduler &) = delete;

    /**
     * @brief scheduler is non-copyable.
     */
    scheduler &operator=(const scheduler &) = delete;

    /**
     * @brief Post a generic callable to be executed by the scheduler loop.
     *
     * This is the slower generic path intended for ordinary callbacks.
     * Coroutine resumptions should prefer post(std::coroutine_handle<>).
     *
     * @tparam Fn Callable type.
     * @param fn Callable to enqueue.
     */
    template <typename Fn>
    void post(Fn &&fn)
    {
      {
        std::lock_guard<std::mutex> lock(m_);
        fn_q_.emplace_back(std::forward<Fn>(fn));
      }

      cv_.notify_one();
    }

    /**
     * @brief Post a coroutine continuation to be resumed by the scheduler.
     *
     * This is the fast path for coroutine scheduling. The handle is stored
     * directly without wrapping it into a generic callable.
     *
     * @param h Coroutine handle to resume.
     */
    void post(std::coroutine_handle<> h) noexcept
    {
      if (!h)
      {
        return;
      }

      {
        std::lock_guard<std::mutex> lock(m_);
        handle_q_.emplace_back(h);
      }

      cv_.notify_one();
    }

    /**
     * @brief Explicit fast-path alias for coroutine continuation posting.
     *
     * @param h Coroutine handle to resume.
     */
    void post_handle(std::coroutine_handle<> h) noexcept
    {
      post(h);
    }

    /**
     * @brief Awaitable used to hop onto the scheduler thread.
     *
     * Typical usage inside a coroutine:
     * @code
     * co_await sched.schedule();
     * @endcode
     *
     * When awaited, the continuation is posted to the scheduler so that
     * it resumes on the scheduler's run() thread.
     */
    struct schedule_awaitable
    {
      /**
       * @brief Target scheduler.
       */
      scheduler *s{};

      /**
       * @brief Always suspend to ensure the continuation is enqueued.
       *
       * @return false (always suspends).
       */
      bool await_ready() const noexcept { return false; }

      /**
       * @brief Enqueue the coroutine continuation.
       *
       * If the scheduler pointer is null, the coroutine is resumed inline
       * as a safe fallback.
       *
       * @param h Coroutine handle to resume.
       */
      void await_suspend(std::coroutine_handle<> h) const noexcept
      {
        if (!s)
        {
          if (h)
          {
            h.resume();
          }
          return;
        }

        s->post_handle(h);
      }

      /**
       * @brief Resume point after scheduling.
       */
      void await_resume() const noexcept {}
    };

    /**
     * @brief Create an awaitable that schedules the awaiting coroutine on this scheduler.
     *
     * @return schedule_awaitable bound to this scheduler.
     */
    schedule_awaitable schedule() noexcept
    {
      return schedule_awaitable{this};
    }

    /**
     * @brief Run the scheduler event loop on the current thread.
     *
     * This function blocks, waiting for new work. It executes coroutine
     * handles first, then generic callables, until stop() is requested
     * and both queues are drained.
     */
    void run()
    {
      running_.store(true, std::memory_order_release);

      while (true)
      {
        std::coroutine_handle<> h{};
        std::function<void()> fn{};

        {
          std::unique_lock<std::mutex> lock(m_);
          cv_.wait(lock, [this]()
                   { return stop_requested_.load(std::memory_order_acquire) ||
                            !handle_q_.empty() ||
                            !fn_q_.empty(); });

          if (!handle_q_.empty())
          {
            h = handle_q_.front();
            handle_q_.pop_front();
          }
          else if (!fn_q_.empty())
          {
            fn = std::move(fn_q_.front());
            fn_q_.pop_front();
          }
          else if (stop_requested_.load(std::memory_order_acquire))
          {
            break;
          }
        }

        if (h)
        {
          h.resume();
          continue;
        }

        if (fn)
        {
          fn();
        }
      }

      running_.store(false, std::memory_order_release);
    }

    /**
     * @brief Request the scheduler loop to stop.
     *
     * Wakes all waiters so that run() can observe the stop request.
     * Pending work already queued will still be drained before exit.
     */
    void stop() noexcept
    {
      stop_requested_.store(true, std::memory_order_release);
      cv_.notify_all();
    }

    /**
     * @brief Reset the scheduler stop state.
     *
     * Useful only if the scheduler is intentionally reused after a previous stop.
     * Must not be called while run() is active.
     */
    void reset() noexcept
    {
      stop_requested_.store(false, std::memory_order_release);
    }

    /**
     * @brief Check whether the scheduler loop is currently running.
     *
     * @return true if run() is active, false otherwise.
     */
    bool is_running() const noexcept
    {
      return running_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check whether stop was requested.
     *
     * @return true if stop() has been called, false otherwise.
     */
    bool stop_requested() const noexcept
    {
      return stop_requested_.load(std::memory_order_acquire);
    }

    /**
     * @brief Return the number of pending coroutine handles currently in the queue.
     *
     * @return Handle queue size.
     */
    std::size_t pending_handles() const
    {
      std::lock_guard<std::mutex> lock(m_);
      return handle_q_.size();
    }

    /**
     * @brief Return the number of pending generic callables currently in the queue.
     *
     * @return Callable queue size.
     */
    std::size_t pending_functions() const
    {
      std::lock_guard<std::mutex> lock(m_);
      return fn_q_.size();
    }

    /**
     * @brief Return the total number of pending work items currently in the queues.
     *
     * @return Total queue size.
     */
    std::size_t pending() const
    {
      std::lock_guard<std::mutex> lock(m_);
      return handle_q_.size() + fn_q_.size();
    }

  private:
    /**
     * @brief Mutex protecting internal queues.
     */
    mutable std::mutex m_;

    /**
     * @brief Condition variable used to wake run() when work arrives or stop is requested.
     */
    std::condition_variable cv_;

    /**
     * @brief FIFO queue dedicated to coroutine continuations.
     *
     * This is the hot path of the async runtime.
     */
    std::deque<std::coroutine_handle<>> handle_q_;

    /**
     * @brief FIFO queue for generic callbacks.
     *
     * This is the slower fallback path for ordinary callables.
     */
    std::deque<std::function<void()>> fn_q_;

    /**
     * @brief Stop request flag observed by run().
     */
    std::atomic<bool> stop_requested_{false};

    /**
     * @brief Indicates whether run() is currently active.
     */
    std::atomic<bool> running_{false};
  };

} // namespace vix::async::core

#endif // VIX_ASYNC_SCHEDULER_HPP
