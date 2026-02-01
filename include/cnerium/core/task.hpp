#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <cassert>

#include <cnerium/core/scheduler.hpp>

namespace cnerium::core
{

  template <typename T>
  class task;

  namespace detail
  {
    struct promise_common
    {
      std::coroutine_handle<> continuation{};
      std::exception_ptr exception{};

      std::suspend_always initial_suspend() noexcept { return {}; }

      struct final_awaiter
      {
        bool await_ready() noexcept
        {
          return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
        {
          auto cont = h.promise().continuation;

          if (!cont)
          {
            h.destroy();
            return std::noop_coroutine();
          }

          return cont;
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
          return std::move(*(p.value));
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

    // Start this task on a scheduler thread.
    // Important: this consumes the task object (rvalue) because the task must stay alive
    // while running. So you typically do:
    //   auto t = foo();
    //   std::move(t).start(sched);
    void start(scheduler &s) && noexcept
    {
      auto h = h_;
      h_ = {};
      if (h)
        s.post(h);
    }

    void start(scheduler &sched) && noexcept
    {
      if (!h_)
        return;

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

    void start(scheduler &s) && noexcept
    {
      auto h = h_;
      h_ = {};
      if (h)
        s.post(h);
    }

    void start(scheduler &sched) && noexcept
    {
      if (!h_)
        return;

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

} // namespace cnerium::core
