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

namespace vix::async::core
{

  thread_pool::thread_pool(io_context &ctx, std::size_t threads)
      : ctx_(ctx)
  {
    if (threads == 0)
    {
      threads = 1;
    }

    workers_.reserve(threads);

    for (std::size_t i = 0; i < threads; ++i)
    {
      workers_.emplace_back([this]()
                            { worker_loop(); });
    }
  }

  thread_pool::~thread_pool() noexcept
  {
    shutdown();
  }

  void thread_pool::submit(std::function<void()> fn)
  {
    if (!fn)
    {
      return;
    }

    enqueue(std::move(fn));
  }

  void thread_pool::stop() noexcept
  {
    {
      std::lock_guard<std::mutex> lock(m_);
      stop_ = true;
    }

    cv_.notify_all();
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

    {
      std::lock_guard<std::mutex> lock(m_);
      stop_ = true;
    }
    cv_.notify_all();

    const std::thread::id self_id = std::this_thread::get_id();

    for (auto &t : workers_)
    {
      if (!t.joinable())
      {
        continue;
      }

      if (t.get_id() == self_id)
      {
        t.detach();
        continue;
      }

      try
      {
        t.join();
      }
      catch (...)
      {
        try
        {
          t.detach();
        }
        catch (...)
        {
        }
      }
    }

    workers_.clear();
  }

  void thread_pool::enqueue(std::function<void()> fn)
  {
    {
      std::lock_guard<std::mutex> lock(m_);

      if (stop_)
      {
        return;
      }

      q_.push_back(std::move(fn));
    }

    cv_.notify_one();
  }

  void thread_pool::worker_loop()
  {
    while (true)
    {
      std::function<void()> fn;

      {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&]()
                 { return stop_ || !q_.empty(); });

        if (!q_.empty())
        {
          fn = std::move(q_.front());
          q_.pop_front();
        }
        else if (stop_)
        {
          break;
        }
      }

      if (fn)
      {
        try
        {
          fn();
        }
        catch (...)
        {
        }
      }
    }
  }

  void thread_pool::ctx_post(std::coroutine_handle<> h)
  {
    ctx_.post(h);
  }

} // namespace vix::async::core
