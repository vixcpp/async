/**
 *
 *  @file io_context.hpp
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
#ifndef VIX_ASYNC_IO_CONTEXT_HPP
#define VIX_ASYNC_IO_CONTEXT_HPP

#include <atomic>
#include <coroutine>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <vix/async/core/scheduler.hpp>

namespace vix::async::net::detail
{
  class asio_net_service;
}

namespace vix::async::core
{
  class thread_pool;
  class timer;
  class signal_set;

  /**
   * @brief Central asynchronous execution context.
   *
   * io_context is the core coordination object of the async runtime.
   *
   * It provides:
   * - a scheduler for coroutine and task execution
   * - a CPU thread pool for blocking or heavy work
   * - a timer service for delayed execution
   * - a signal handling service
   * - a networking backend (asio-based)
   *
   * Services are lazily initialized and owned by the context.
   *
   * Lifecycle:
   * - Services are created on first use
   * - shutdown() stops the scheduler and destroys all services
   * - destruction is safe and idempotent
   *
   * Thread-safety:
   * - posting tasks is thread-safe
   * - lazy service initialization is serialized
   * - shutdown is serialized and idempotent
   * - services cannot be recreated after shutdown
   */
  class io_context
  {
  public:
    /**
     * @brief Construct an empty io_context.
     */
    io_context();

    /**
     * @brief Destroy the io_context and release all resources.
     *
     * Automatically calls shutdown() if not already done.
     */
    ~io_context() noexcept;

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    /**
     * @brief Access the internal scheduler.
     *
     * @return Reference to scheduler.
     */
    [[nodiscard]] scheduler &get_scheduler() noexcept
    {
      return sched_;
    }

    /**
     * @brief Access the internal scheduler (const).
     *
     * @return Const reference to scheduler.
     */
    [[nodiscard]] const scheduler &get_scheduler() const noexcept
    {
      return sched_;
    }

    /**
     * @brief Post a generic callable task to the scheduler.
     *
     * This uses the scheduler generic path and is intended for ordinary callbacks.
     *
     * @tparam Fn Callable type.
     * @param fn Task to execute.
     */
    template <typename Fn>
    void post(Fn &&fn)
    {
      sched_.post(std::forward<Fn>(fn));
    }

    /**
     * @brief Post a coroutine handle to the scheduler fast path.
     *
     * @param h Coroutine handle to resume.
     */
    void post(std::coroutine_handle<> h) noexcept
    {
      sched_.post_handle(h);
    }

    /**
     * @brief Explicit fast-path API for coroutine resumption.
     *
     * @param h Coroutine handle to resume.
     */
    void post_handle(std::coroutine_handle<> h) noexcept
    {
      sched_.post_handle(h);
    }

    /**
     * @brief Run the scheduler loop.
     *
     * Blocks until stop() is called.
     */
    void run()
    {
      sched_.run();
    }

    /**
     * @brief Stop the scheduler.
     *
     * Causes run() to exit after draining pending work.
     */
    void stop() noexcept
    {
      sched_.stop();
    }

    /**
     * @brief Check whether the scheduler is running.
     *
     * @return true if running, false otherwise.
     */
    [[nodiscard]] bool is_running() const noexcept
    {
      return sched_.is_running();
    }

    /**
     * @brief Access the CPU thread pool.
     *
     * Lazily initialized on first access.
     *
     * @return Reference to thread_pool.
     *
     * @throws std::runtime_error If the context has already been shut down.
     */
    [[nodiscard]] thread_pool &cpu_pool();

    /**
     * @brief Access the timer service.
     *
     * Lazily initialized on first access.
     *
     * @return Reference to timer.
     *
     * @throws std::runtime_error If the context has already been shut down.
     */
    [[nodiscard]] timer &timers();

    /**
     * @brief Access the signal handling service.
     *
     * Lazily initialized on first access.
     *
     * @return Reference to signal_set.
     *
     * @throws std::runtime_error If the context has already been shut down.
     */
    [[nodiscard]] signal_set &signals();

    /**
     * @brief Access the networking backend service.
     *
     * Lazily initialized on first access.
     *
     * @return Reference to asio_net_service.
     *
     * @throws std::runtime_error If the context has already been shut down.
     */
    [[nodiscard]] vix::async::net::detail::asio_net_service &net();

    /**
     * @brief Stop scheduler and destroy all services.
     *
     * This function:
     * - stops the scheduler
     * - destroys all lazily created services
     *
     * It is safe to call multiple times.
     */
    void shutdown() noexcept;

  private:
    /**
     * @brief Check whether shutdown has already completed.
     *
     * @return true if the context is shut down, false otherwise.
     */
    [[nodiscard]] bool is_shutdown() const noexcept
    {
      return shutdown_done_.load(std::memory_order_acquire);
    }

    /**
     * @brief Ensure that the context is still usable.
     *
     * @throws std::runtime_error If the context has already been shut down.
     */
    void ensure_not_shutdown() const
    {
      if (is_shutdown())
      {
        throw std::runtime_error("io_context is shut down");
      }
    }

  private:
    /** @brief Core scheduler. */
    scheduler sched_;

    /** @brief CPU thread pool (lazy). */
    std::unique_ptr<thread_pool> cpu_pool_;

    /** @brief Timer service (lazy). */
    std::unique_ptr<timer> timer_;

    /** @brief Signal handling service (lazy). */
    std::unique_ptr<signal_set> signals_;

    /** @brief Networking backend (lazy). */
    std::unique_ptr<vix::async::net::detail::asio_net_service> net_;

    /** @brief Ensures shutdown runs once. */
    std::atomic<bool> shutdown_done_{false};

    /**
     * @brief Protects lifecycle operations.
     *
     * This mutex serializes:
     * - lazy service initialization
     * - shutdown
     *
     * It prevents races between service creation and destruction.
     */
    mutable std::mutex lifecycle_mutex_;
  };

} // namespace vix::async::core

#endif // VIX_ASYNC_IO_CONTEXT_HPP
