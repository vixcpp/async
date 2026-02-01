#include <cnerium/core/signal.hpp>
#include <cnerium/core/io_context.hpp>

#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <signal.h>
#endif

namespace cnerium::core
{

  signal_set::signal_set(io_context &ctx)
      : ctx_(ctx)
  {
  }

  signal_set::~signal_set()
  {
    stop();
    if (worker_.joinable())
      worker_.join();
  }

  void signal_set::add(int sig)
  {
    std::lock_guard<std::mutex> lock(m_);
    signals_.push_back(sig);
  }

  void signal_set::remove(int sig)
  {
    std::lock_guard<std::mutex> lock(m_);
    for (auto it = signals_.begin(); it != signals_.end();)
    {
      if (*it == sig)
        it = signals_.erase(it);
      else
        ++it;
    }
  }

  void signal_set::on_signal(std::function<void(int)> fn)
  {
    std::lock_guard<std::mutex> lock(m_);
    on_signal_ = std::move(fn);
  }

  void signal_set::stop() noexcept
  {
    {
      std::lock_guard<std::mutex> lock(m_);
      stop_ = true;
    }

#if defined(__unix__) || defined(__APPLE__)
    ::pthread_kill(worker_.native_handle(), SIGTERM);
#endif
  }

  void signal_set::start_if_needed()
  {
    std::lock_guard<std::mutex> lock(m_);
    if (started_)
      return;

    started_ = true;
    worker_ = std::thread([this]()
                          { worker_loop(); });
  }

  void signal_set::ctx_post(std::function<void()> fn)
  {
    ctx_.post(std::move(fn));
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
      signal_set *self;
      cancel_token ct;

      int sig{0};

      bool await_ready()
      {
        std::lock_guard<std::mutex> lock(self->m_);
        if (!self->pending_.empty())
        {
          sig = self->pending_.front();
          self->pending_.pop();
          return true;
        }
        return false;
      }

      void await_suspend(std::coroutine_handle<> h)
      {
        std::lock_guard<std::mutex> lock(self->m_);

        if (ct.is_cancelled())
        {
          self->ctx_post([h]() mutable
                         { if (h) h.resume(); });
          return;
        }

        if (!self->pending_.empty())
        {
          sig = self->pending_.front();
          self->pending_.pop();
          self->ctx_post([h]() mutable
                         { if (h) h.resume(); });
          return;
        }

        self->waiter_ = h;
        self->waiter_active_ = true;
      }

      int await_resume()
      {
        if (ct.is_cancelled())
          throw std::system_error(cancelled_ec());

        return sig;
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
      std::vector<int> sigs_copy;
      {
        std::lock_guard<std::mutex> lock(m_);
        if (stop_)
          return;

        sigs_copy = signals_;
      }

      if (sigs_copy.empty())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }

      sigset_t set;
      ::sigemptyset(&set);
      for (int s : sigs_copy)
        ::sigaddset(&set, s);

      ::pthread_sigmask(SIG_BLOCK, &set, nullptr);

      int received = 0;
      int rc = ::sigwait(&set, &received);

      if (rc != 0)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      {
        std::lock_guard<std::mutex> lock(m_);
        if (stop_)
          return;

        pending_.push(received);
      }

      ctx_post([this]()
               {
      int sig = 0;
      std::function<void(int)> handler;
      std::coroutine_handle<> waiter;
      bool has_waiter = false;

      {
        std::lock_guard<std::mutex> lock(m_);

        if (pending_.empty())
          return;

        sig = pending_.front();
        pending_.pop();

        handler = on_signal_;

        if (waiter_active_)
        {
          waiter = waiter_;
          waiter_ = {};
          waiter_active_ = false;
          has_waiter = true;
        }
      }

      if (handler)
        handler(sig);

      if (has_waiter && waiter)
      {
        {
          std::lock_guard<std::mutex> lock(m_);
          pending_.push(sig);
        }
        waiter.resume();
      }
      else
      {
        std::lock_guard<std::mutex> lock(m_);
        pending_.push(sig);
      } });
    }
#endif
  }

  signal_set &io_context::signals()
  {
    if (!signals_)
    {
      signals_ = std::make_unique<signal_set>(*this);
    }
    return *signals_;
  }

} // namespace cnerium::core
