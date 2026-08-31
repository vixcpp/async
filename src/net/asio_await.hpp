/**
 *
 *  @file asio_await.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the LICENSE file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_ASYNC_ASIO_AWAIT_HPP
#define VIX_ASYNC_ASIO_AWAIT_HPP

#include <atomic>
#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>
#include <vix/async/core/io_context.hpp>

#include "asio_net_service.hpp"

namespace vix::async::net::detail
{
  template <typename T>
  struct asio_result
  {
    std::error_code ec{};
    std::optional<T> value{};
  };

  template <>
  struct asio_result<void>
  {
    std::error_code ec{};
  };

  inline std::system_error to_system_error(const std::error_code &ec)
  {
    return std::system_error(ec);
  }

  inline void resume_on_ctx(
      vix::async::core::io_context *ctx,
      std::coroutine_handle<> h) noexcept
  {
    if (!h)
    {
      return;
    }

    if (!ctx)
    {
      h.resume();
      return;
    }

    ctx->post_handle(h);
  }

  template <typename Starter, typename T>
  struct asio_awaitable
  {
    vix::async::core::io_context *ctx{};
    asio_net_service *service{};
    vix::async::core::cancel_token ct{};
    Starter starter;
    std::function<void()> cancel_action{};

    asio_result<T> res{};
    std::exception_ptr ex{};
    vix::async::core::cancel_registration cancel_registration{};
    asio_net_service::operation_registration service_registration{};
    std::atomic<int> phase{0};
    std::atomic<bool> stopped_by_service{false};

    bool await_ready() const noexcept
    {
      return false;
    }

    void complete(std::coroutine_handle<> h) noexcept
    {
      int expected = 0;
      if (phase.compare_exchange_strong(
              expected,
              2,
              std::memory_order_acq_rel,
              std::memory_order_acquire))
      {
        return;
      }

      expected = 1;
      if (phase.compare_exchange_strong(
              expected,
              2,
              std::memory_order_acq_rel,
              std::memory_order_acquire))
      {
        resume_on_ctx(ctx, h);
      }
    }

    void arm_or_resume(std::coroutine_handle<> h) noexcept
    {
      int expected = 0;
      if (phase.compare_exchange_strong(
              expected,
              1,
              std::memory_order_acq_rel,
              std::memory_order_acquire))
      {
        return;
      }

      if (expected == 2)
      {
        resume_on_ctx(ctx, h);
      }
    }

    void await_suspend(std::coroutine_handle<> h)
    {
      if (ct.is_cancelled())
      {
        res.ec = vix::async::core::cancelled_ec();
        complete(h);
        arm_or_resume(h);
        return;
      }

      bool operation_started = false;

      try
      {
        if constexpr (std::is_void_v<T>)
        {
          starter(
              [this, h](std::error_code ec) mutable
              {
                res.ec = ec;
                complete(h);
              });
        }
        else
        {
          starter(
              [this, h](std::error_code ec, T value) mutable
              {
                res.ec = ec;

                if (!ec)
                {
                  res.value.emplace(std::move(value));
                }

                complete(h);
              });
        }

        operation_started = true;
      }
      catch (...)
      {
        ex = std::current_exception();
        complete(h);
      }

      if (operation_started && cancel_action)
      {
        cancel_registration = ct.on_cancel(
            [cancel = cancel_action]() mutable
            {
              try
              {
                cancel();
              }
              catch (...)
              {
              }
            });

        if (service)
        {
          service_registration = service->register_operation(
              [this, cancel = cancel_action]() mutable
              {
                stopped_by_service.store(true, std::memory_order_release);
                try
                {
                  cancel();
                }
                catch (...)
                {
                }
              });
        }
      }

      arm_or_resume(h);
    }

    T await_resume()
    {
      cancel_registration.reset();
      service_registration.reset();

      if (ct.is_cancelled())
      {
        throw std::system_error(vix::async::core::cancelled_ec());
      }

      if (stopped_by_service.load(std::memory_order_acquire))
      {
        throw std::system_error(
            vix::async::core::make_error_code(vix::async::core::errc::stopped));
      }

      if (ex)
      {
        std::rethrow_exception(ex);
      }

      if (res.ec)
      {
        throw to_system_error(res.ec);
      }

      if constexpr (std::is_void_v<T>)
      {
        return;
      }
      else
      {
        return std::move(*res.value);
      }
    }
  };

} // namespace vix::async::net::detail

#endif // VIX_ASYNC_ASIO_AWAIT_HPP
