/**
 *
 *  @file spawn.hpp
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
#ifndef VIX_ASYNC_SPAWN_HPP
#define VIX_ASYNC_SPAWN_HPP

#include <coroutine>
#include <utility>

#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>

namespace vix::async::core
{
  namespace detail
  {
    /**
     * @brief Internal coroutine type used to run a task<void> in detached mode.
     *
     * detached_task is a small helper coroutine that:
     * - starts suspended (initial_suspend)
     * - resumes when posted to the scheduler
     * - destroys itself at final suspension
     *
     * It is intentionally move-only and designed to be fire-and-forget.
     */
    struct detached_task
    {
      /**
       * @brief Promise type for detached_task.
       *
       * The coroutine:
       * - returns a detached_task that carries its coroutine handle
       * - starts suspended so the caller can schedule it explicitly
       * - self-destroys on final suspend
       * - swallows exceptions to avoid terminating the runtime
       */
      struct promise_type
      {
        /**
         * @brief Create the coroutine return object.
         *
         * @return detached_task owning the coroutine handle.
         */
        detached_task get_return_object() noexcept
        {
          return detached_task{
              std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /**
         * @brief Start suspended.
         *
         * This allows posting the coroutine handle to a scheduler explicitly.
         *
         * @return std::suspend_always
         */
        std::suspend_always initial_suspend() noexcept
        {
          return {};
        }

        /**
         * @brief Final awaiter that destroys the coroutine frame.
         *
         * This ensures detached coroutines clean up automatically once done.
         */
        struct final_awaiter
        {
          /**
           * @brief Always suspend at final suspend to run await_suspend.
           *
           * @return false
           */
          bool await_ready() noexcept
          {
            return false;
          }

          /**
           * @brief Destroy the coroutine frame.
           *
           * @param h Coroutine handle to destroy.
           */
          void await_suspend(std::coroutine_handle<promise_type> h) noexcept
          {
            h.destroy();
          }

          /**
           * @brief No result to return.
           */
          void await_resume() noexcept {}
        };

        /**
         * @brief Use a final awaiter that self-destroys.
         *
         * @return final_awaiter
         */
        final_awaiter final_suspend() noexcept
        {
          return {};
        }

        /**
         * @brief Return void.
         */
        void return_void() noexcept {}

        /**
         * @brief Handle unobserved exceptions.
         *
         * Detached tasks cannot propagate exceptions to a caller. This hook
         * intentionally swallows exceptions to keep the runtime alive.
         */
        void unhandled_exception() noexcept
        {
          // Detached tasks intentionally swallow exceptions.
        }
      };

      /**
       * @brief Coroutine handle for the detached task.
       */
      std::coroutine_handle<promise_type> h{};

      /**
       * @brief Construct from a coroutine handle.
       *
       * @param hh Coroutine handle.
       */
      explicit detached_task(std::coroutine_handle<promise_type> hh) noexcept
          : h(hh)
      {
      }

      /**
       * @brief Move construct.
       *
       * @param o Source task.
       */
      detached_task(detached_task &&o) noexcept
          : h(o.h)
      {
        o.h = {};
      }

      /**
       * @brief Move assign.
       *
       * @param o Source task.
       * @return Reference to this.
       */
      detached_task &operator=(detached_task &&o) noexcept
      {
        if (this != &o)
        {
          h = o.h;
          o.h = {};
        }
        return *this;
      }

      /**
       * @brief detached_task is non-copyable.
       */
      detached_task(const detached_task &) = delete;

      /**
       * @brief detached_task is non-copyable.
       */
      detached_task &operator=(const detached_task &) = delete;

      /**
       * @brief Default destructor.
       *
       * The coroutine frame is destroyed by final_suspend, not by this destructor.
       */
      ~detached_task() = default;
    };

    /**
     * @brief Wrap a task<void> into a detached coroutine.
     *
     * The returned detached_task awaits the provided task and then completes,
     * destroying itself at final suspend.
     *
     * @param t Task to execute.
     * @return detached_task owning the coroutine handle.
     */
    inline detached_task make_detached(task<void> t)
    {
      co_await t;
      co_return;
    }
  } // namespace detail

  /**
   * @brief Spawn a task<void> and detach it onto an io_context scheduler.
   *
   * This function schedules the provided task on the io_context without
   * returning a handle to join or await it. The coroutine self-destroys on
   * completion and exceptions are swallowed by design.
   *
   * @param ctx Runtime context used to schedule execution.
   * @param t Task to run in detached mode.
   */
  inline void spawn_detached(io_context &ctx, task<void> t)
  {
    auto dt = detail::make_detached(std::move(t));
    ctx.post_handle(dt.h);
  }

} // namespace vix::async::core

#endif // VIX_ASYNC_SPAWN_HPP
