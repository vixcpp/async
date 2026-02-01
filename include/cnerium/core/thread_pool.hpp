#pragma once

#include <cstddef>
#include <coroutine>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <vector>
#include <utility>

#include <cnerium/core/error.hpp>
#include <cnerium/core/task.hpp>
#include <cnerium/core/cancel.hpp>

namespace cnerium::core
{
  class io_context;

  class thread_pool
  {
  public:
    explicit thread_pool(io_context &ctx, std::size_t threads = std::thread::hardware_concurrency());
    ~thread_pool();

    thread_pool(const thread_pool &) = delete;
    thread_pool &operator=(const thread_pool &) = delete;
    void submit(std::function<void()> fn);
    template <typename Fn>
    auto submit(Fn &&fn, cancel_token ct = {}) -> task<std::invoke_result_t<Fn>>
    {
      using R = std::invoke_result_t<Fn>;
      struct awaitable
      {
        thread_pool *pool;
        cancel_token ct;
        std::decay_t<Fn> fn;

        std::optional<R> result{};
        std::exception_ptr ex{};

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h)
        {
          pool->enqueue(
              [this, h]() mutable
              {
              try
              {
                if (ct.is_cancelled())
                  throw std::system_error(cancelled_ec());

                if constexpr (std::is_void_v<R>)
                {
                  fn();
                }
                else
                {
                  result.emplace(fn());
                }
              }
              catch (...)
              {
                ex = std::current_exception();
              }

              pool->ctx_post(h); });
        }

        R await_resume()
        {
          if (ex)
            std::rethrow_exception(ex);

          if constexpr (std::is_void_v<R>)
          {
            return;
          }
          else
          {
            return std::move(*result);
          }
        }
      };

      co_return co_await awaitable{this, ct, std::forward<Fn>(fn)};
    }

    void stop() noexcept;
    std::size_t size() const noexcept { return workers_.size(); }

  private:
    void worker_loop();
    void enqueue(std::function<void()> fn);
    void ctx_post(std::coroutine_handle<> h);

  private:
    io_context &ctx_;

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;

    bool stop_{false};
    std::vector<std::thread> workers_;
  };

} // namespace cnerium::core
