#pragma once

#include <coroutine>

#include <cnerium/core/io_context.hpp>
#include <cnerium/core/task.hpp>

namespace cnerium::core
{
  namespace detail
  {
    struct detached_task
    {
      struct promise_type
      {
        detached_task get_return_object() noexcept
        {
          return detached_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct final_awaiter
        {
          bool await_ready() noexcept { return false; }

          void await_suspend(std::coroutine_handle<promise_type> h) noexcept
          {
            h.destroy();
          }

          void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { return {}; }

        void return_void() noexcept {}

        void unhandled_exception() noexcept
        {
          // Detached: swallow exceptions to keep runtime alive.
          // Later: hook into logger / error reporting.
        }
      };

      std::coroutine_handle<promise_type> h{};

      explicit detached_task(std::coroutine_handle<promise_type> hh) noexcept : h(hh) {}

      detached_task(detached_task &&o) noexcept : h(o.h) { o.h = {}; }
      detached_task &operator=(detached_task &&o) noexcept
      {
        if (this != &o)
        {
          h = o.h;
          o.h = {};
        }
        return *this;
      }

      detached_task(const detached_task &) = delete;
      detached_task &operator=(const detached_task &) = delete;
      ~detached_task() = default;
    };

    inline detached_task make_detached(task<void> t)
    {
      co_await t;
      co_return;
    }
  } // namespace detail

  inline void spawn_detached(io_context &ctx, task<void> t)
  {
    auto dt = detail::make_detached(std::move(t));
    ctx.post(dt.h);
  }

} // namespace cnerium::core
