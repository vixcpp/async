/**
 *
 *  @file task.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_ASYNC_TASK_HPP
#define VIX_ASYNC_TASK_HPP

#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

#include <vix/async/core/scheduler.hpp>

namespace vix::async::core
{
  /**
   * @brief Coroutine task type representing an asynchronous computation.
   *
   * task<T> models a coroutine that eventually produces a value of type T,
   * or throws an exception. It is move-only and owns its coroutine frame.
   *
   * Key properties:
   * - Lazy: tasks start suspended and only run when awaited or explicitly started
   * - Single-consumer: the produced value is moved out on await_resume()
   * - Exception-aware: exceptions are captured and rethrown on await_resume()
   * - Detachable: start(scheduler) schedules the coroutine and releases ownership
   *
   * @tparam T Result type (must not be a reference).
   */
  template <typename T>
  class task;

  namespace detail
  {
    /**
     * @brief Common promise state shared by task<void> and task<T>.
     *
     * Stores:
     * - continuation: awaiting coroutine to resume at final suspend
     * - exception: captured exception to rethrow on await_resume()
     * - detached: if true, coroutine frame self-destroys when completed
     */
    struct promise_common
    {
      /**
       * @brief Continuation coroutine to resume after completion.
       */
      std::coroutine_handle<> continuation{};

      /**
       * @brief Captured exception (if any).
       */
      std::exception_ptr exception{};

      /**
       * @brief Detached flag.
       *
       * When detached is true and there is no continuation, the coroutine
       * frame is destroyed at final suspension.
       */
      bool detached{false};

      /**
       * @brief Start suspended.
       *
       * Tasks are lazy by default and require awaiting or explicit scheduling.
       *
       * @return std::suspend_always
       */
      std::suspend_always initial_suspend() noexcept
      {
        return {};
      }

      /**
       * @brief Final awaiter responsible for resuming continuation or self-destruction.
       */
      struct final_awaiter
      {
        /**
         * @brief Always suspend to control continuation transfer.
         */
        bool await_ready() noexcept
        {
          return false;
        }

        /**
         * @brief Resume the continuation or destroy if detached.
         *
         * If a continuation exists, it is returned so the runtime resumes it.
         * If detached is true and there is no continuation, the coroutine
         * frame destroys itself.
         *
         * @tparam Promise Promise type of the coroutine.
         * @param h Coroutine handle.
         * @return Next coroutine to resume.
         */
        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
        {
          auto &p = h.promise();

          if (p.continuation)
          {
            return p.continuation;
          }

          if (p.detached)
          {
            h.destroy();
          }

          return std::noop_coroutine();
        }

        /**
         * @brief No result to return.
         */
        void await_resume() noexcept {}
      };

      /**
       * @brief Use the final awaiter described above.
       */
      final_awaiter final_suspend() noexcept
      {
        return {};
      }

      /**
       * @brief Capture unhandled exceptions.
       */
      void unhandled_exception() noexcept
      {
        exception = std::current_exception();
      }
    };

    /**
     * @brief Promise type for task<T>.
     *
     * Stores an optional value to hold the result until it is consumed.
     *
     * @tparam T Result type (must not be a reference).
     */
    template <typename T>
    struct promise_value : promise_common
    {
      static_assert(
          !std::is_reference_v<T>,
          "task<T> does not support reference T (use task<std::reference_wrapper<T>>).");

      /**
       * @brief Create the task return object.
       *
       * @return task<T> bound to this promise.
       */
      task<T> get_return_object() noexcept;

      /**
       * @brief Store the returned value into the promise.
       *
       * @tparam U Value type.
       * @param v Value to store.
       */
      template <typename U>
      void return_value(U &&v) noexcept(std::is_nothrow_constructible_v<T, U &&>)
      {
        value.emplace(std::forward<U>(v));
      }

      /**
       * @brief Stored result value.
       */
      std::optional<T> value{};
    };

    /**
     * @brief Promise type specialization for task<void>.
     */
    template <>
    struct promise_value<void> : promise_common
    {
      /**
       * @brief Create the task return object.
       *
       * @return task<void> bound to this promise.
       */
      task<void> get_return_object() noexcept;

      /**
       * @brief Complete without a value.
       */
      void return_void() noexcept {}
    };

    /**
     * @brief Awaiter for task<T> and task<void>.
     *
     * This awaiter:
     * - resumes the task coroutine when awaited
     * - stores the awaiting coroutine as continuation
     * - rethrows captured exceptions in await_resume()
     * - moves the produced value out for task<T>
     *
     * @tparam Promise Promise type (promise_value<T> or promise_value<void>).
     */
    template <typename Promise>
    struct task_awaiter
    {
      /**
       * @brief Coroutine handle of the awaited task.
       */
      std::coroutine_handle<Promise> h{};

      /**
       * @brief Ready if there is no handle or the coroutine already completed.
       */
      bool await_ready() const noexcept
      {
        return !h || h.done();
      }

      /**
       * @brief Suspend the awaiting coroutine and resume the task coroutine.
       *
       * @param awaiting Awaiting coroutine handle.
       * @return Coroutine handle to resume next (the task coroutine).
       */
      std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept
      {
        h.promise().continuation = awaiting;
        return h;
      }

      /**
       * @brief Resume and extract the result (or rethrow exception).
       *
       * @return For task<T>, returns T moved. For task<void>, returns void.
       * @throws Any exception captured by the task coroutine.
       */
      decltype(auto) await_resume()
      {
        auto &p = h.promise();

        if (p.exception)
        {
          std::rethrow_exception(p.exception);
        }

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

  /**
   * @brief Coroutine task representing an asynchronous computation producing T.
   *
   * task<T> owns its coroutine handle and destroys the coroutine frame
   * when the task object is destroyed, unless it has been detached via start().
   *
   * @tparam T Result type.
   */
  template <typename T>
  class task
  {
  public:
    /**
     * @brief Promise type used by the coroutine.
     */
    using promise_type = detail::promise_value<T>;

    /**
     * @brief Coroutine handle type.
     */
    using handle_type = std::coroutine_handle<promise_type>;

    /**
     * @brief Construct an empty task.
     */
    task() noexcept = default;

    /**
     * @brief Construct from a coroutine handle.
     *
     * @param h Coroutine handle.
     */
    explicit task(handle_type h) noexcept : h_(h) {}

    /**
     * @brief Move construct.
     *
     * @param other Source task.
     */
    task(task &&other) noexcept : h_(other.h_)
    {
      other.h_ = {};
    }

    /**
     * @brief Move assign.
     *
     * Destroys any currently owned coroutine frame before taking ownership.
     *
     * @param other Source task.
     * @return Reference to this.
     */
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

    /**
     * @brief task is non-copyable.
     */
    task(const task &) = delete;

    /**
     * @brief task is non-copyable.
     */
    task &operator=(const task &) = delete;

    /**
     * @brief Destroy the task and its coroutine frame unless detached.
     */
    ~task()
    {
      destroy();
    }

    /**
     * @brief Check whether this task holds a coroutine handle.
     *
     * @return true if valid, false otherwise.
     */
    bool valid() const noexcept
    {
      return static_cast<bool>(h_);
    }

    /**
     * @brief Bool conversion.
     */
    explicit operator bool() const noexcept
    {
      return valid();
    }

    /**
     * @brief co_await support (lvalue).
     */
    auto operator co_await() & noexcept
    {
      return detail::task_awaiter<promise_type>{h_};
    }

    /**
     * @brief co_await support (rvalue).
     */
    auto operator co_await() && noexcept
    {
      return detail::task_awaiter<promise_type>{h_};
    }

    /**
     * @brief Access the underlying coroutine handle.
     *
     * @return Coroutine handle.
     */
    handle_type handle() const noexcept
    {
      return h_;
    }

    /**
     * @brief Release ownership of the coroutine handle.
     *
     * @return Released handle.
     */
    handle_type release() noexcept
    {
      handle_type tmp = h_;
      h_ = {};
      return tmp;
    }

    /**
     * @brief Start the task on a scheduler and detach it.
     *
     * Marks the coroutine as detached and posts it onto the scheduler fast path.
     * After this call, the task releases ownership and becomes empty.
     *
     * @param sched Scheduler used to run the task.
     */
    void start(scheduler &sched) && noexcept
    {
      if (!h_)
      {
        return;
      }

      h_.promise().detached = true;
      sched.post_handle(std::coroutine_handle<>(h_));
      h_ = {};
    }

  private:
    /**
     * @brief Destroy the coroutine frame if owned.
     */
    void destroy() noexcept
    {
      if (h_)
      {
        h_.destroy();
        h_ = {};
      }
    }

    /**
     * @brief Owned coroutine handle.
     */
    handle_type h_{};
  };

  /**
   * @brief Coroutine task specialization for void result.
   *
   * Same ownership and lifecycle rules as task<T>, but without a value.
   */
  template <>
  class task<void>
  {
  public:
    /**
     * @brief Promise type used by the coroutine.
     */
    using promise_type = detail::promise_value<void>;

    /**
     * @brief Coroutine handle type.
     */
    using handle_type = std::coroutine_handle<promise_type>;

    /**
     * @brief Construct an empty task.
     */
    task() noexcept = default;

    /**
     * @brief Construct from a coroutine handle.
     *
     * @param h Coroutine handle.
     */
    explicit task(handle_type h) noexcept : h_(h) {}

    /**
     * @brief Move construct.
     *
     * @param other Source task.
     */
    task(task &&other) noexcept : h_(other.h_)
    {
      other.h_ = {};
    }

    /**
     * @brief Move assign.
     *
     * @param other Source task.
     * @return Reference to this.
     */
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

    /**
     * @brief task is non-copyable.
     */
    task(const task &) = delete;

    /**
     * @brief task is non-copyable.
     */
    task &operator=(const task &) = delete;

    /**
     * @brief Destroy the task and its coroutine frame unless detached.
     */
    ~task()
    {
      destroy();
    }

    /**
     * @brief Check whether this task holds a coroutine handle.
     *
     * @return true if valid, false otherwise.
     */
    bool valid() const noexcept
    {
      return static_cast<bool>(h_);
    }

    /**
     * @brief Bool conversion.
     */
    explicit operator bool() const noexcept
    {
      return valid();
    }

    /**
     * @brief co_await support (lvalue).
     */
    auto operator co_await() & noexcept
    {
      return detail::task_awaiter<promise_type>{h_};
    }

    /**
     * @brief co_await support (rvalue).
     */
    auto operator co_await() && noexcept
    {
      return detail::task_awaiter<promise_type>{h_};
    }

    /**
     * @brief Access the underlying coroutine handle.
     *
     * @return Coroutine handle.
     */
    handle_type handle() const noexcept
    {
      return h_;
    }

    /**
     * @brief Release ownership of the coroutine handle.
     *
     * @return Released handle.
     */
    handle_type release() noexcept
    {
      handle_type tmp = h_;
      h_ = {};
      return tmp;
    }

    /**
     * @brief Start the task on a scheduler and detach it.
     *
     * Marks the coroutine as detached and posts it onto the scheduler fast path.
     *
     * @param sched Scheduler used to run the task.
     */
    void start(scheduler &sched) && noexcept
    {
      if (!h_)
      {
        return;
      }

      h_.promise().detached = true;
      sched.post_handle(std::coroutine_handle<>(h_));
      h_ = {};
    }

  private:
    /**
     * @brief Destroy the coroutine frame if owned.
     */
    void destroy() noexcept
    {
      if (h_)
      {
        h_.destroy();
        h_ = {};
      }
    }

    /**
     * @brief Owned coroutine handle.
     */
    handle_type h_{};
  };

  namespace detail
  {
    /**
     * @brief Create task<T> from its promise.
     *
     * @tparam T Result type.
     * @return task<T> bound to this promise.
     */
    template <typename T>
    inline task<T> promise_value<T>::get_return_object() noexcept
    {
      using handle_t = std::coroutine_handle<promise_value<T>>;
      return task<T>(handle_t::from_promise(*this));
    }

    /**
     * @brief Create task<void> from its promise.
     *
     * @return task<void> bound to this promise.
     */
    inline task<void> promise_value<void>::get_return_object() noexcept
    {
      using handle_t = std::coroutine_handle<promise_value<void>>;
      return task<void>(handle_t::from_promise(*this));
    }
  } // namespace detail

} // namespace vix::async::core

#endif // VIX_ASYNC_TASK_HPP
