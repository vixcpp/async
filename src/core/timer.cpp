/**
 *
 *  @file timer.cpp
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
#include <vix/async/core/timer.hpp>
#include <vix/async/core/io_context.hpp>

#include <atomic>
#include <coroutine>
#include <functional>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace vix::async::core
{
  namespace
  {
    struct sleep_state
    {
      std::atomic<bool> completed{false};
      std::atomic<errc> outcome{errc::ok};
      std::coroutine_handle<> continuation{};
      cancel_registration cancellation{};
    };
  } // namespace

  timer::timer(io_context &ctx)
      : ctx_(ctx),
        worker_(
            [this]()
            {
              timer_loop();
            })
  {
  }

  timer::~timer()
  {
    stop();

    if (!worker_.joinable())
    {
      return;
    }

    const auto self_id = std::this_thread::get_id();

    if (worker_.get_id() == self_id)
    {
      worker_.detach();
      return;
    }

    try
    {
      worker_.join();
    }
    catch (...)
    {
      try
      {
        worker_.detach();
      }
      catch (...)
      {
      }
    }
  }

  void timer::stop() noexcept
  {
    std::vector<std::function<void()>> stop_handlers;

    {
      std::lock_guard<std::mutex> lock(m_);

      if (stop_)
      {
        return;
      }

      stop_ = true;

      for (auto it = q_.begin(); it != q_.end(); ++it)
      {
        auto &entry_ref = const_cast<entry &>(*it);
        if (entry_ref.on_stop)
        {
          stop_handlers.emplace_back(std::move(entry_ref.on_stop));
        }
      }

      q_.clear();
    }

    cv_.notify_all();

    for (auto &handler : stop_handlers)
    {
      try
      {
        if (handler)
        {
          handler();
        }
      }
      catch (...)
      {
      }
    }
  }

  bool timer::stopped() const noexcept
  {
    std::lock_guard<std::mutex> lock(m_);
    return stop_;
  }

  void timer::schedule(
      time_point tp,
      std::unique_ptr<job> j,
      cancel_token ct,
      std::function<void()> on_stop)
  {
    if (!j)
    {
      return;
    }

    bool rejected = false;

    {
      std::lock_guard<std::mutex> lock(m_);

      if (stop_)
      {
        rejected = true;
      }
      else
      {
        entry e;
        e.when = tp;
        e.id = ++seq_;
        e.ct = std::move(ct);
        e.j = std::move(j);
        e.on_stop = std::move(on_stop);

        q_.insert(std::move(e));
      }
    }

    if (rejected)
    {
      if (on_stop)
      {
        try
        {
          on_stop();
        }
        catch (...)
        {
        }
      }
      return;
    }

    cv_.notify_all();
  }

  task<void> timer::sleep_for(duration d, cancel_token ct)
  {
    struct awaitable
    {
      timer *self{};
      duration delay{};
      cancel_token token{};
      std::shared_ptr<sleep_state> state{};

      bool await_ready()
      {
        if (token.is_cancelled())
        {
          return true;
        }

        if (self->stopped())
        {
          return true;
        }

        return delay <= duration::zero();
      }

      void await_suspend(std::coroutine_handle<> h)
      {
        state = std::make_shared<sleep_state>();
        state->continuation = h;

        auto finish = [timer_self = self,
                       weak = std::weak_ptr<sleep_state>(state)](errc result) mutable
        {
          auto shared = weak.lock();
          if (!shared)
          {
            return;
          }

          bool expected = false;
          if (!shared->completed.compare_exchange_strong(
                  expected,
                  true,
                  std::memory_order_acq_rel,
                  std::memory_order_acquire))
          {
            return;
          }

          shared->outcome.store(result, std::memory_order_release);

          if (timer_self && shared->continuation)
          {
            timer_self->ctx_post_handle(shared->continuation);
          }
        };

        state->cancellation = token.on_cancel(
            [finish]() mutable
            {
              finish(errc::canceled);
            });

        self->schedule(
            clock::now() + delay,
            make_job(
                [finish]() mutable
                {
                  finish(errc::ok);
                }),
            {},
            [finish]() mutable
            {
              finish(errc::stopped);
            });
      }

      void await_resume()
      {
        if (!state)
        {
          if (token.is_cancelled())
          {
            throw std::system_error(cancelled_ec());
          }

          if (self->stopped())
          {
            throw std::system_error(make_error_code(errc::stopped));
          }

          return;
        }

        state->cancellation.reset();

        switch (state->outcome.load(std::memory_order_acquire))
        {
        case errc::ok:
          return;
        case errc::canceled:
          throw std::system_error(cancelled_ec());
        case errc::stopped:
          throw std::system_error(make_error_code(errc::stopped));
        default:
          throw std::system_error(make_error_code(errc::stopped));
        }
      }
    };

    co_return co_await awaitable{this, d, std::move(ct)};
  }

  void timer::ctx_post(std::function<void()> fn)
  {
    ctx_.post(std::move(fn));
  }

  void timer::ctx_post_handle(std::coroutine_handle<> h)
  {
    ctx_.post_handle(h);
  }

  void timer::timer_loop()
  {
    while (true)
    {
      entry next{};
      bool has_next = false;

      {
        std::unique_lock<std::mutex> lock(m_);

        cv_.wait(
            lock,
            [this]()
            {
              return stop_ || !q_.empty();
            });

        if (stop_)
        {
          break;
        }

        auto it = q_.begin();
        auto &front = const_cast<entry &>(*it);
        next.when = front.when;
        next.id = front.id;
        next.ct = front.ct;
        next.j = std::move(front.j);
        next.on_stop = std::move(front.on_stop);
        q_.erase(it);
        has_next = true;
      }

      if (!has_next)
      {
        continue;
      }

      while (true)
      {
        const auto now = clock::now();
        if (now >= next.when)
        {
          break;
        }

        std::unique_lock<std::mutex> lock(m_);

        if (stop_)
        {
          if (next.on_stop)
          {
            auto on_stop = std::move(next.on_stop);
            lock.unlock();
            on_stop();
          }
          return;
        }

        if (!q_.empty())
        {
          auto it = q_.begin();
          if (it->when < next.when)
          {
            q_.insert(entry{
                next.when,
                next.id,
                next.ct,
                std::move(next.j),
                std::move(next.on_stop)});

            auto &front = const_cast<entry &>(*it);
            next.when = front.when;
            next.id = front.id;
            next.ct = front.ct;
            next.j = std::move(front.j);
            next.on_stop = std::move(front.on_stop);
            q_.erase(it);
            continue;
          }
        }

        cv_.wait_until(lock, next.when);

        if (stop_)
        {
          if (next.on_stop)
          {
            auto on_stop = std::move(next.on_stop);
            lock.unlock();
            on_stop();
          }
          return;
        }
      }

      if (next.ct.is_cancelled())
      {
        continue;
      }

      if (next.j)
      {
        std::shared_ptr<job> j(next.j.release());

        ctx_post(
            [j = std::move(j)]() mutable
            {
              if (j)
              {
                j->run();
              }
            });
      }
    }
  }

} // namespace vix::async::core
