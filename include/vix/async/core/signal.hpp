/**
 *
 *  @file signal.hpp
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
#ifndef VIX_ASYNC_SIGNAL_HPP
#define VIX_ASYNC_SIGNAL_HPP

#include <csignal>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>
#include <vix/async/core/task.hpp>

namespace vix::async::core
{
  class io_context;

  /**
   * @brief Asynchronous POSIX signal watcher integrated with io_context.
   *
   * signal_set provides a small signal-watching facility that:
   * - allows registering a set of signal numbers (add/remove)
   * - supports awaiting the next signal via async_wait()
   * - optionally invokes a callback on each received signal
   * - posts completions back onto the io_context scheduler
   *
   * Internally, it may start a dedicated worker thread on first use
   * (lazy startup) to observe signals and dispatch events safely.
   *
   * @note Exact signal delivery semantics depend on platform rules and
   * process/thread signal masks. This API is designed to integrate with
   * Vix async tasks and cancellation.
   */
  class signal_set
  {
  public:
    /**
     * @brief Construct a signal_set bound to an io_context.
     *
     * @param ctx Runtime context used to post completions back to the scheduler.
     */
    explicit signal_set(io_context &ctx);

    /**
     * @brief Destroy the signal_set.
     *
     * Stops the internal worker if started and releases resources.
     */
    ~signal_set();

    /**
     * @brief signal_set is non-copyable.
     */
    signal_set(const signal_set &) = delete;

    /**
     * @brief signal_set is non-copyable.
     */
    signal_set &operator=(const signal_set &) = delete;

    /**
     * @brief Add a signal number to be observed.
     *
     * If the watcher is not started yet, it may be started lazily.
     *
     * @param sig Signal number such as SIGINT.
     */
    void add(int sig);

    /**
     * @brief Remove a signal number from observation.
     *
     * @param sig Signal number previously added.
     */
    void remove(int sig);

    /**
     * @brief Asynchronously wait for the next received signal.
     *
     * The returned task completes with the signal number once available.
     * If a signal was already captured and queued, the task may complete
     * quickly by consuming from the pending queue.
     *
     * @param ct Cancellation token.
     * @return task<int> completing with the received signal number.
     */
    task<int> async_wait(cancel_token ct = {});

    /**
     * @brief Register a callback invoked when a signal is received.
     *
     * The callback is executed via the io_context posting mechanism
     * so it runs on the scheduler thread, not on the worker thread.
     *
     * @param fn Callback taking the signal number.
     */
    void on_signal(std::function<void(int)> fn);

    /**
     * @brief Stop signal watching and wake any waiter.
     *
     * This requests shutdown of the internal worker loop if running.
     */
    void stop() noexcept;

  private:
    /**
     * @brief Start the worker thread if not already started.
     */
    void start_if_needed();

    /**
     * @brief Worker thread loop.
     *
     * Captures signals, pushes them into the pending queue, and
     * triggers waiter or callback dispatch onto the io_context.
     */
    void worker_loop();

    /**
     * @brief Post a generic callback onto the io_context scheduler.
     *
     * @param fn Function to execute on the scheduler thread.
     */
    void ctx_post(std::function<void()> fn);

    /**
     * @brief Post a coroutine handle onto the io_context fast coroutine path.
     *
     * @param h Coroutine handle to resume.
     */
    void ctx_post_handle(std::coroutine_handle<> h);

  private:
    /**
     * @brief Bound runtime context.
     */
    io_context &ctx_;

    /**
     * @brief Mutex protecting internal state.
     */
    std::mutex m_;

    /**
     * @brief List of observed signals.
     */
    std::vector<int> signals_;

    /**
     * @brief Optional callback invoked per received signal.
     */
    std::function<void(int)> on_signal_{};

    /**
     * @brief Queue of captured signals waiting to be delivered.
     */
    std::queue<int> pending_;

    /**
     * @brief Indicates whether the worker thread was started.
     */
    bool started_{false};

    /**
     * @brief Stop request flag observed by the worker loop.
     */
    bool stop_{false};

    /**
     * @brief Worker thread used to observe signals.
     */
    std::thread worker_;

    /**
     * @brief Coroutine handle waiting for a signal single waiter model.
     */
    std::coroutine_handle<> waiter_{};

    /**
     * @brief Whether a waiter coroutine is currently active.
     */
    bool waiter_active_{false};
  };

} // namespace vix::async::core

#endif // VIX_ASYNC_SIGNAL_HPP
