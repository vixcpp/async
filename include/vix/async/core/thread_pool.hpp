/**
 *
 *  @file thread_pool.hpp
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
#ifndef VIX_ASYNC_THREAD_POOL_HPP
#define VIX_ASYNC_THREAD_POOL_HPP

#include <cstddef>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <optional>
#include <system_error>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>
#include <vix/async/core/task.hpp>

namespace vix::async::core
{
  class io_context;

  namespace detail
  {
    template <typename R>
    struct result_store
    {
      std::optional<R> value{};
      void set(R &&v) { value.emplace(std::move(v)); }
      R take() { return std::move(*value); }
    };

    template <>
    struct result_store<void>
    {
      void set() noexcept {}
    };
  }

  class thread_pool
  {
  public:
    explicit thread_pool(
        io_context &ctx,
        std::size_t threads = std::thread::hardware_concurrency());
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
        thread_pool *pool{};
        cancel_token ct{};
        std::decay_t<Fn> fn;

        detail::result_store<R> store{};
        std::exception_ptr ex{};

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h)
        {
          pool->enqueue([this, h]() mutable
                        {
        try
        {
          if (ct.is_cancelled())
            throw std::system_error(cancelled_ec());

          if constexpr (std::is_void_v<R>)
          {
            fn();
            store.set();
          }
          else
          {
            R r = fn();
            store.set(std::move(r));
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
            return store.take();
          }
        }
      };

      co_return co_await awaitable{this, std::move(ct), std::forward<Fn>(fn)};
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

} // namespace vix::async::core

#endif
