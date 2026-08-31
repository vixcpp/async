/**
 *
 *  @file thread_pool.hpp
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
#ifndef VIX_ASYNC_THREAD_POOL_HPP
#define VIX_ASYNC_THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>
#include <vix/async/core/task.hpp>

namespace vix::async::core
{
  class io_context;

  namespace detail
  {
    /**
     * @brief Storage helper for non-void async task results.
     *
     * This small helper stores the result produced by a submitted callable
     * until the awaiting coroutine resumes and extracts it.
     *
     * @tparam R Result type.
     */
    template <typename R>
    struct result_store
    {
      /** @brief Stored result value. */
      std::optional<R> value{};

      /**
       * @brief Store a result value.
       *
       * @param v Result to store.
       */
      void set(R &&v)
      {
        value.emplace(std::move(v));
      }

      /**
       * @brief Extract the stored result.
       *
       * @return Stored result.
       */
      R take()
      {
        return std::move(*value);
      }
    };

    /**
     * @brief Storage helper specialization for void results.
     *
     * No actual value storage is required for void-returning tasks.
     */
    template <>
    struct result_store<void>
    {
      /**
       * @brief Record successful completion for a void task.
       */
      void set() noexcept {}
    };
  } // namespace detail

  /**
   * @brief Lightweight CPU thread pool for async runtime offloading.
   *
   * This pool is used to execute blocking or CPU-heavy callables outside the
   * main async scheduler, while resuming coroutines back on the owning
   * io_context.
   *
   * Design goals:
   * - simple FIFO execution
   * - safe shutdown
   * - coroutine-friendly submission
   * - idempotent destruction
   *
   * The pool owns a fixed set of worker threads created at construction time.
   */
  class thread_pool
  {
    struct shared_state;

  public:
    /**
     * @brief Construct a thread pool attached to an io_context.
     *
     * @param ctx Owning io_context used to resume coroutines.
     * @param threads Number of worker threads to create.
     *        If zero, at least one worker is created.
     */
    explicit thread_pool(
        io_context &ctx,
        std::size_t threads = std::thread::hardware_concurrency());

    /**
     * @brief Destroy the thread pool safely.
     *
     * Automatically calls shutdown() if needed.
     */
    ~thread_pool() noexcept;

    thread_pool(const thread_pool &) = delete;
    thread_pool &operator=(const thread_pool &) = delete;

    /**
     * @brief Submit a plain callable for background execution.
     *
     * The callable is queued and executed by one worker thread.
     *
     * @param fn Callable to execute.
     */
    [[nodiscard]] bool post(std::function<void()> fn);

    /**
     * @brief Submit a callable and await its result as a coroutine task.
     *
     * The callable is executed on the thread pool, then the awaiting coroutine
     * is resumed back on the owning io_context fast coroutine path.
     *
     * If the cancellation token is already cancelled before execution starts,
     * the coroutine resumes with a cancelled system_error.
     *
     * @tparam Fn Callable type.
     * @param fn Callable to execute.
     * @param ct Optional cancellation token.
     *
     * @return Async task producing the callable result type.
     */
    template <typename Fn>
    auto submit(Fn &&fn, cancel_token ct = {})
        -> task<std::invoke_result_t<std::decay_t<Fn> &>>
    {
      using function_type = std::decay_t<Fn>;
      using R = std::invoke_result_t<function_type &>;

      return submit_impl<function_type, R>(
          state_,
          &ctx_,
          function_type(std::forward<Fn>(fn)),
          std::move(ct));
    }

    /**
     * @brief Return whether the pool has accepted a stop request.
     *
     * @return true once stop() or shutdown() has been requested.
     */
    [[nodiscard]] bool stopped() const noexcept;

    /**
     * @brief Request the pool to stop accepting and processing new work.
     *
     * This wakes all workers so they can exit once pending work is drained
     * according to the worker loop logic.
     */
    void stop() noexcept;

    /**
     * @brief Stop the pool and join all workers safely.
     *
     * This operation is idempotent and safe to call multiple times.
     * It also protects against self-join during destruction.
     */
    void shutdown() noexcept;

    /**
     * @brief Return the number of worker threads owned by the pool.
     *
     * @return Number of workers.
     */
    [[nodiscard]] std::size_t size() const noexcept
    {
      return workers_.size();
    }

  private:
    template <typename Fn, typename R>
    static task<R> submit_impl(
        std::shared_ptr<shared_state> state,
        io_context *ctx,
        Fn fn,
        cancel_token ct)
    {

      struct awaitable
      {
        /** @brief Shared worker state used to enqueue the callable. */
        std::shared_ptr<shared_state> state{};

        /** @brief Context used to resume the awaiting coroutine. */
        io_context *ctx{};

        /** @brief Optional cancellation token. */
        cancel_token ct{};

        /** @brief Callable to execute in the pool. */
        std::decay_t<Fn> fn;

        /** @brief Storage for the callable result. */
        detail::result_store<R> store{};

        /** @brief Captured exception, if execution fails. */
        std::exception_ptr ex{};

        /**
         * @brief Always suspend and dispatch work to the pool.
         *
         * @return false to force suspension.
         */
        bool await_ready() const noexcept
        {
          return false;
        }

        /**
         * @brief Enqueue the callable and resume the coroutine later.
         *
         * @param h Awaiting coroutine handle.
         */
        void await_suspend(std::coroutine_handle<> h)
        {
          const bool accepted = thread_pool::enqueue_shared(
              state,
              [this, h]() mutable
              {
                try
                {
                  if (ct.is_cancelled())
                  {
                    throw std::system_error(cancelled_ec());
                  }

                  if constexpr (std::is_void_v<R>)
                  {
                    fn();
                    store.set();
                  }
                  else
                  {
                    R r = fn();
                    store.set(std::move(r));
                  }
                }
                catch (...)
                {
                  ex = std::current_exception();
                }

                thread_pool::post_to_context(ctx, h);
              });

          if (!accepted)
          {
            ex = std::make_exception_ptr(
                std::system_error(make_error_code(errc::rejected)));
            thread_pool::post_to_context(ctx, h);
          }
        }

        /**
         * @brief Resume and return or rethrow the execution result.
         *
         * @return Result produced by the callable.
         *
         * @throw Any exception captured during execution.
         */
        R await_resume()
        {
          if (ex)
          {
            std::rethrow_exception(ex);
          }

          if constexpr (std::is_void_v<R>)
          {
            return;
          }
          else
          {
            return store.take();
          }
        }
      };

      co_return co_await awaitable{
          std::move(state),
          ctx,
          std::move(ct),
          std::move(fn)};
    }

    /**
     * @brief Worker thread main loop.
     *
     * Waits for queued work, executes callables, and exits when shutdown is
     * requested and no work remains.
     */
    static void worker_loop(std::shared_ptr<shared_state> state);

    /**
     * @brief Enqueue one callable into the worker queue.
     *
     * @param fn Callable to enqueue.
     */
    static bool enqueue_shared(
        const std::shared_ptr<shared_state> &state,
        std::function<void()> fn);

    /**
     * @brief Post a coroutine handle back to the owning io_context fast path.
     *
     * @param h Coroutine handle to resume.
     */
    static void post_to_context(
        io_context *ctx,
        std::coroutine_handle<> h) noexcept;

  private:
    /** @brief Owning io_context used for coroutine resumption. */
    io_context &ctx_;

    /** @brief Shared state retained by worker threads during shutdown. */
    std::shared_ptr<shared_state> state_;

    /** @brief Owned worker threads. */
    std::vector<std::thread> workers_;

    /**
     * @brief Ensures shutdown and worker joining happen only once.
     */
    std::atomic<bool> shutdown_done_{false};
  };

} // namespace vix::async::core

#endif // VIX_ASYNC_THREAD_POOL_HPP
