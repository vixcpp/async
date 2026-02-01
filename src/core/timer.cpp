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

#include <functional>
#include <system_error>
#include <memory>

namespace vix::async::core
{
  timer::timer(io_context &ctx)
      : ctx_(ctx),
        worker_([this]()
                { timer_loop(); })
  {
  }

  timer::~timer()
  {
    stop();
    if (worker_.joinable())
      worker_.join();
  }

  void timer::stop() noexcept
  {
    {
      std::lock_guard<std::mutex> lock(m_);
      stop_ = true;

      q_.clear();
    }
    cv_.notify_all();
  }

  void timer::schedule(time_point tp, std::unique_ptr<job> j, cancel_token ct)
  {
    if (!j)
      return;

    {
      std::lock_guard<std::mutex> lock(m_);
      if (stop_)
        return;

      entry e;
      e.when = tp;
      e.id = ++seq_;
      e.ct = std::move(ct);
      e.j = std::move(j);

      q_.insert(std::move(e));
    }

    cv_.notify_all();
  }

  task<void> timer::sleep_for(duration d, cancel_token ct)
  {
    struct awaitable
    {
      timer *self;
      duration d;
      cancel_token ct;

      bool await_ready() const noexcept { return d.count() == 0; }

      void await_suspend(std::coroutine_handle<> h)
      {
        self->after(d, [h]() mutable
                    { if (h) h.resume(); }, ct);
      }

      void await_resume()
      {
        if (ct.is_cancelled())
          throw std::system_error(cancelled_ec());
      }
    };

    co_return co_await awaitable{this, d, std::move(ct)};
  }

  void timer::ctx_post(std::function<void()> fn)
  {
    ctx_.post(std::move(fn));
  }

  void timer::timer_loop()
  {
    while (true)
    {
      entry next{};
      bool has_next = false;

      {
        std::unique_lock<std::mutex> lock(m_);

        cv_.wait(lock, [&]()
                 { return stop_ || !q_.empty(); });

        if (stop_)
        {
          break;
        }

        auto it = q_.begin();
        next = entry{it->when, it->id, it->ct, nullptr};
        next.j = std::move(const_cast<entry &>(*it).j);
        q_.erase(it);
        has_next = true;
      }

      if (!has_next)
        continue;

      while (true)
      {
        auto now = clock::now();
        if (now >= next.when)
          break;

        std::unique_lock<std::mutex> lock(m_);
        if (stop_)
          return;

        if (!q_.empty())
        {
          auto it = q_.begin();
          if (it->when < next.when)
          {
            q_.insert(entry{next.when, next.id, next.ct, std::move(next.j)});

            next = entry{it->when, it->id, it->ct, nullptr};
            next.j = std::move(const_cast<entry &>(*it).j);
            q_.erase(it);
            continue;
          }
        }

        cv_.wait_until(lock, next.when, [&]()
                       { return stop_; });
        if (stop_)
          return;
      }

      if (next.ct.is_cancelled())
      {
        continue;
      }

      if (next.j)
      {
        std::shared_ptr<job> j(next.j.release());
        ctx_post([j = std::move(j)]() mutable
                 {
             if (j)
               j->run(); });
      }
    }
  }

} // namespace vix::async::core
