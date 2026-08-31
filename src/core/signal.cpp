/**
 *
 *  @file signal.cpp
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
#include <vix/async/core/signal.hpp>
#include <vix/async/core/io_context.hpp>

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <signal.h>
#endif

namespace vix::async::core
{
  signal_set::signal_set(io_context &ctx)
      : ctx_(ctx)
  {
  }

  signal_set::~signal_set()
  {
    stop();

    if (!worker_.joinable())
    {
      return;
    }

    const auto self_id = std::this_thread::get_id();

    if (worker_.get_id() == self_id)
    {
      try
      {
        worker_.detach();
      }
      catch (...)
      {
      }
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

  void signal_set::add(int sig)
  {
    if (sig <= 0)
    {
      throw std::system_error(make_error_code(errc::invalid_argument));
    }

#if defined(__unix__) || defined(__APPLE__)
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
#endif

    std::lock_guard<std::mutex> lock(m_);

    if (std::find(signals_.begin(), signals_.end(), sig) == signals_.end())
    {
      signals_.push_back(sig);
    }
  }

  void signal_set::remove(int sig)
  {
    std::lock_guard<std::mutex> lock(m_);
    signals_.erase(
        std::remove(signals_.begin(), signals_.end(), sig),
        signals_.end());
  }

  void signal_set::on_signal(std::function<void(int)> fn)
  {
    std::lock_guard<std::mutex> lock(m_);
    on_signal_ = std::move(fn);
  }

  void signal_set::stop() noexcept
  {
    std::shared_ptr<wait_state> waiter;
    int wake_signal = 0;

    {
      std::lock_guard<std::mutex> lock(m_);

      if (stop_)
      {
        return;
      }

      stop_ = true;
      wake_signal = wake_signal_;
      waiter = std::move(waiter_);
    }

    if (waiter &&
        !waiter->completed.exchange(true, std::memory_order_acq_rel))
    {
      waiter->outcome = errc::stopped;
      ctx_post_handle(waiter->continuation);
    }

#if defined(__unix__) || defined(__APPLE__)
    if (wake_signal > 0 && worker_.joinable())
    {
      try
      {
        pthread_kill(worker_.native_handle(), wake_signal);
      }
      catch (...)
      {
      }
    }
#else
    (void)wake_signal;
#endif
  }

  void signal_set::start_if_needed()
  {
    std::lock_guard<std::mutex> lock(m_);

    if (started_ || stop_)
    {
      return;
    }

    started_ = true;
    worker_ = std::thread(
        [this]()
        {
          worker_loop();
        });
  }

  void signal_set::ctx_post(std::function<void()> fn)
  {
    ctx_.post(std::move(fn));
  }

  void signal_set::ctx_post_handle(std::coroutine_handle<> h)
  {
    ctx_.post_handle(h);
  }

  task<int> signal_set::async_wait(cancel_token ct)
  {
#if !(defined(__unix__) || defined(__APPLE__))
    (void)ct;
    throw std::system_error(make_error_code(errc::not_supported));
#else
    start_if_needed();

    struct awaitable
    {
      signal_set *self{};
      cancel_token token{};
      std::shared_ptr<wait_state> state{std::make_shared<wait_state>()};
      cancel_registration cancellation{};

      bool await_ready()
      {
        std::lock_guard<std::mutex> lock(self->m_);

        if (token.is_cancelled())
        {
          state->outcome = errc::canceled;
          state->completed.store(true, std::memory_order_release);
          return true;
        }

        if (self->stop_)
        {
          state->outcome = errc::stopped;
          state->completed.store(true, std::memory_order_release);
          return true;
        }

        if (!self->pending_.empty())
        {
          state->signal = self->pending_.front();
          self->pending_.pop();
          state->outcome = errc::ok;
          state->completed.store(true, std::memory_order_release);
          return true;
        }

        return false;
      }

      void await_suspend(std::coroutine_handle<> h)
      {
        state->continuation = h;
        bool resume_now = false;

        {
          std::lock_guard<std::mutex> lock(self->m_);

          if (token.is_cancelled())
          {
            state->outcome = errc::canceled;
            state->completed.store(true, std::memory_order_release);
            resume_now = true;
          }
          else if (self->stop_)
          {
            state->outcome = errc::stopped;
            state->completed.store(true, std::memory_order_release);
            resume_now = true;
          }
          else if (!self->pending_.empty())
          {
            state->signal = self->pending_.front();
            self->pending_.pop();
            state->outcome = errc::ok;
            state->completed.store(true, std::memory_order_release);
            resume_now = true;
          }
          else if (self->waiter_)
          {
            state->outcome = errc::not_ready;
            state->completed.store(true, std::memory_order_release);
            resume_now = true;
          }
          else
          {
            self->waiter_ = state;
          }
        }

        if (resume_now)
        {
          self->ctx_post_handle(h);
          return;
        }

        cancellation = token.on_cancel(
            [signal_self = self,
             weak = std::weak_ptr<wait_state>(state)]()
            {
              auto shared = weak.lock();
              if (!shared)
              {
                return;
              }

              {
                std::lock_guard<std::mutex> lock(signal_self->m_);
                if (signal_self->waiter_ == shared)
                {
                  signal_self->waiter_.reset();
                }
              }

              if (!shared->completed.exchange(true, std::memory_order_acq_rel))
              {
                shared->outcome = errc::canceled;
                signal_self->ctx_post_handle(shared->continuation);
              }
            });
      }

      int await_resume()
      {
        cancellation.reset();

        switch (state->outcome)
        {
        case errc::ok:
          return state->signal;
        case errc::canceled:
          throw std::system_error(cancelled_ec());
        case errc::stopped:
          throw std::system_error(make_error_code(errc::stopped));
        case errc::not_ready:
          throw std::system_error(make_error_code(errc::not_ready));
        default:
          throw std::system_error(make_error_code(state->outcome));
        }
      }
    };

    co_return co_await awaitable{this, std::move(ct)};
#endif
  }

  void signal_set::worker_loop()
  {
#if !(defined(__unix__) || defined(__APPLE__))
    return;
#else
    while (true)
    {
      std::vector<int> signals;

      {
        std::lock_guard<std::mutex> lock(m_);

        if (stop_)
        {
          return;
        }

        signals = signals_;
      }

      if (signals.empty())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }

      sigset_t set;
      sigemptyset(&set);

      for (int signal : signals)
      {
        sigaddset(&set, signal);
      }

      pthread_sigmask(SIG_BLOCK, &set, nullptr);

      {
        std::lock_guard<std::mutex> lock(m_);
        if (stop_)
        {
          return;
        }
        wake_signal_ = signals.front();
      }

      int received = 0;
      const int rc = sigwait(&set, &received);

      std::shared_ptr<wait_state> waiter;
      std::function<void(int)> handler;

      {
        std::lock_guard<std::mutex> lock(m_);
        wake_signal_ = 0;

        if (stop_)
        {
          return;
        }

        if (rc != 0)
        {
          continue;
        }

        handler = on_signal_;

        if (waiter_)
        {
          waiter = std::move(waiter_);
        }
        else
        {
          pending_.push(received);
        }
      }

      if (handler)
      {
        ctx_post(
            [handler = std::move(handler), received]() mutable
            {
              handler(received);
            });
      }

      if (waiter &&
          !waiter->completed.exchange(true, std::memory_order_acq_rel))
      {
        waiter->signal = received;
        waiter->outcome = errc::ok;
        ctx_post_handle(waiter->continuation);
      }
    }
#endif
  }

} // namespace vix::async::core
