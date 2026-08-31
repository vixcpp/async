/**
 *
 *  @file thread_pool.cpp
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
#include <vix/async/core/thread_pool.hpp>
#include <vix/async/core/io_context.hpp>

#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace vix::async::core
{
  struct thread_pool::shared_state
  {
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> queue;
    bool stop{false};
  };

  thread_pool::thread_pool(io_context &ctx, std::size_t threads)
      : ctx_(ctx),
        state_(std::make_shared<shared_state>())
  {
    if (threads == 0)
    {
      threads = 1;
    }

    workers_.reserve(threads);

    for (std::size_t i = 0; i < threads; ++i)
    {
      auto state = state_;
      workers_.emplace_back(
          [state = std::move(state)]() mutable
          {
            worker_loop(std::move(state));
          });
    }
  }

  thread_pool::~thread_pool() noexcept
  {
    shutdown();
  }

  bool thread_pool::post(std::function<void()> fn)
  {
    if (!fn)
    {
      return false;
    }

    return enqueue_shared(state_, std::move(fn));
  }

  bool thread_pool::stopped() const noexcept
  {
    const auto state = state_;
    if (!state)
    {
      return true;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    return state->stop;
  }

  void thread_pool::stop() noexcept
  {
    const auto state = state_;
    if (!state)
    {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->stop = true;
    }

    state->cv.notify_all();
  }

  void thread_pool::shutdown() noexcept
  {
    bool expected = false;
    if (!shutdown_done_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
      return;
    }

    stop();

    const std::thread::id self_id = std::this_thread::get_id();

    for (auto &thread : workers_)
    {
      if (!thread.joinable())
      {
        continue;
      }

      if (thread.get_id() == self_id)
      {
        try
        {
          thread.detach();
        }
        catch (...)
        {
        }
        continue;
      }

      try
      {
        thread.join();
      }
      catch (...)
      {
        try
        {
          thread.detach();
        }
        catch (...)
        {
        }
      }
    }

    workers_.clear();
  }

  bool thread_pool::enqueue_shared(
      const std::shared_ptr<shared_state> &state,
      std::function<void()> fn)
  {
    if (!state || !fn)
    {
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(state->mutex);

      if (state->stop)
      {
        return false;
      }

      state->queue.emplace_back(std::move(fn));
    }

    state->cv.notify_one();
    return true;
  }

  void thread_pool::worker_loop(std::shared_ptr<shared_state> state)
  {
    if (!state)
    {
      return;
    }

    while (true)
    {
      std::function<void()> fn;

      {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait(
            lock,
            [&state]()
            {
              return state->stop || !state->queue.empty();
            });

        if (!state->queue.empty())
        {
          fn = std::move(state->queue.front());
          state->queue.pop_front();
        }
        else if (state->stop)
        {
          break;
        }
      }

      if (!fn)
      {
        continue;
      }

      try
      {
        fn();
      }
      catch (...)
      {
      }
    }
  }

  void thread_pool::post_to_context(
      io_context *ctx,
      std::coroutine_handle<> h) noexcept
  {
    if (!h)
    {
      return;
    }

    if (!ctx)
    {
      h.resume();
      return;
    }

    ctx->post_handle(h);
  }

} // namespace vix::async::core
