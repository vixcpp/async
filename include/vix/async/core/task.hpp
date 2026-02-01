/**
 *
 *  @file task.hpp
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
#ifndef VIX_ASYNC_TASK_HPP
#define VIX_ASYNC_TASK_HPP

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <cassert>

#include <vix/async/core/scheduler.hpp>

namespace vix::async::core
{
  template <typename T>
  class task;

  namespace detail
  {
    struct promise_common
    {
      std::coroutine_handle<> continuation{};
      std::exception_ptr exception{};

      bool detached{false};

      std::suspend_always initial_suspend() noexcept { return {}; }

      struct final_awaiter
      {
        bool await_ready() noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
        {
          auto &p = h.promise();

          if (p.continuation)
            return p.continuation;

          if (p.detached)
            h.destroy();

          return std::noop_coroutine();
        }

        void await_resume() noexcept {}
      };

      final_awaiter final_suspend() noexcept { return {}; }

      void unhandled_exception() noexcept
      {
        exception = std::current_exception();
      }
    };

    template <typename T>
    struct promise_value : promise_common
    {
      static_assert(!std::is_reference_v<T>,
                    "task<T> does not support reference T (use task<std::reference_wrapper<T>>).");

      task<T> get_return_object() noexcept;

      template <typename U>
      void return_value(U &&v) noexcept(std::is_nothrow_constructible_v<T, U &&>)
      {
        value.emplace(std::forward<U>(v));
      }

      std::optional<T> value{};
    };

    template <>
    struct promise_value<void> : promise_common
    {
      task<void> get_return_object() noexcept;
      void return_void() noexcept {}
    };

    template <typename Promise>
    struct task_awaiter
    {
      std::coroutine_handle<Promise> h{};

      bool await_ready() const noexcept
      {
        return !h || h.done();
      }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept
      {
        h.promise().continuation = awaiting;
        return h;
      }

      decltype(auto) await_resume()
      {
        auto &p = h.promise();

        if (p.exception)
          std::rethrow_exception(p.exception);

        if constexpr (std::is_same_v<Promise, promise_value<void>>)
        {
          return;
        }
        else
        {
          assert(p.value.has_value());
          return std::move(*p.value);
        }
      }
    };

  } // namespace detail

  template <typename T>
  class task
  {
  public:
    using promise_type = detail::promise_value<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;
    explicit task(handle_type h) noexcept : h_(h) {}

    task(task &&other) noexcept : h_(other.h_) { other.h_ = {}; }

    task &operator=(task &&other) noexcept
    {
      if (this != &other)
      {
        destroy();
        h_ = other.h_;
        other.h_ = {};
      }
      return *this;
    }

    task(const task &) = delete;
    task &operator=(const task &) = delete;
    ~task() { destroy(); }
    bool valid() const noexcept { return static_cast<bool>(h_); }
    explicit operator bool() const noexcept { return valid(); }
    auto operator co_await() & noexcept { return detail::task_awaiter<promise_type>{h_}; }
    auto operator co_await() && noexcept { return detail::task_awaiter<promise_type>{h_}; }
    handle_type handle() const noexcept { return h_; }

    void start(scheduler &sched) && noexcept
    {
      if (!h_)
        return;

      h_.promise().detached = true;
      sched.post(std::coroutine_handle<>(h_));
      h_ = {};
    }

  private:
    void destroy() noexcept
    {
      if (h_)
      {
        h_.destroy();
        h_ = {};
      }
    }

    handle_type h_{};
  };

  template <>
  class task<void>
  {
  public:
    using promise_type = detail::promise_value<void>;
    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;
    explicit task(handle_type h) noexcept : h_(h) {}
    task(task &&other) noexcept : h_(other.h_) { other.h_ = {}; }

    task &operator=(task &&other) noexcept
    {
      if (this != &other)
      {
        destroy();
        h_ = other.h_;
        other.h_ = {};
      }
      return *this;
    }

    task(const task &) = delete;
    task &operator=(const task &) = delete;
    ~task() { destroy(); }
    bool valid() const noexcept { return static_cast<bool>(h_); }
    explicit operator bool() const noexcept { return valid(); }
    auto operator co_await() & noexcept { return detail::task_awaiter<promise_type>{h_}; }
    auto operator co_await() && noexcept { return detail::task_awaiter<promise_type>{h_}; }
    handle_type handle() const noexcept { return h_; }

    void start(scheduler &sched) && noexcept
    {
      if (!h_)
        return;

      h_.promise().detached = true;
      sched.post(std::coroutine_handle<>(h_));
      h_ = {};
    }

  private:
    void destroy() noexcept
    {
      if (h_)
      {
        h_.destroy();
        h_ = {};
      }
    }

    handle_type h_{};
  };

  namespace detail
  {
    template <typename T>
    inline task<T> promise_value<T>::get_return_object() noexcept
    {
      using handle_t = std::coroutine_handle<promise_value<T>>;
      return task<T>(handle_t::from_promise(*this));
    }

    inline task<void> promise_value<void>::get_return_object() noexcept
    {
      using handle_t = std::coroutine_handle<promise_value<void>>;
      return task<void>(handle_t::from_promise(*this));
    }
  } // namespace detail

} // namespace vix::async::core

#endif
