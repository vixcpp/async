#pragma once

#include <coroutine>
#include <cstdint>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <utility>

namespace cnerium::core
{
  class scheduler
  {
  public:
    scheduler() = default;

    scheduler(const scheduler &) = delete;
    scheduler &operator=(const scheduler &) = delete;

    template <typename Fn>
    void post(Fn &&fn)
    {
      {
        std::lock_guard<std::mutex> lock(m_);
        q_.emplace_back(job{std::forward<Fn>(fn)});
      }
      cv_.notify_one();
    }

    void post(std::coroutine_handle<> h)
    {
      post([h]() mutable
           { if (h) h.resume(); });
    }

    void run()
    {
      running_ = true;

      while (true)
      {
        job j;

        {
          std::unique_lock<std::mutex> lock(m_);
          cv_.wait(lock, [&]()
                   { return stop_requested_ || !q_.empty(); });

          if (!q_.empty())
          {
            j = std::move(q_.front());
            q_.pop_front();
          }
          else if (stop_requested_)
          {
            break;
          }
        }

        if (j.fn)
        {
          j.fn();
        }
      }

      running_ = false;
    }

    void stop() noexcept
    {
      {
        std::lock_guard<std::mutex> lock(m_);
        stop_requested_ = true;
      }
      cv_.notify_all();
    }

    bool is_running() const noexcept { return running_; }

    std::size_t pending() const
    {
      std::lock_guard<std::mutex> lock(m_);
      return q_.size();
    }

  private:
    struct job
    {
      job() = default;

      template <typename Fn>
      explicit job(Fn &&f) : fn(std::forward<Fn>(f)) {}

      struct fn_base
      {
        virtual ~fn_base() = default;
        virtual void call() = 0;
      };

      template <typename Fn>
      struct fn_impl final : fn_base
      {
        Fn f;
        explicit fn_impl(Fn &&x) : f(std::move(x)) {}
        void call() override { f(); }
      };

      struct fn_holder
      {
        fn_holder() = default;

        template <typename Fn>
        explicit fn_holder(Fn &&f)
        {
          ptr = new fn_impl<std::decay_t<Fn>>(std::forward<Fn>(f));
        }

        fn_holder(fn_holder &&o) noexcept : ptr(o.ptr) { o.ptr = nullptr; }
        fn_holder &operator=(fn_holder &&o) noexcept
        {
          if (this != &o)
          {
            reset();
            ptr = o.ptr;
            o.ptr = nullptr;
          }
          return *this;
        }

        fn_holder(const fn_holder &) = delete;
        fn_holder &operator=(const fn_holder &) = delete;

        ~fn_holder() { reset(); }

        void operator()()
        {
          if (ptr)
            ptr->call();
        }

        explicit operator bool() const noexcept { return ptr != nullptr; }

        void reset() noexcept
        {
          delete ptr;
          ptr = nullptr;
        }

        fn_base *ptr{nullptr};
      };

      fn_holder fn{};
    };

  private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<job> q_;

    bool stop_requested_{false};
    bool running_{false};
  };

} // namespace cnerium::core
