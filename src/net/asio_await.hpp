/**
 *
 *  @file asio_await.hpp
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
#ifndef VIX_ASYNC_ASIO_AWAIT_HPP
#define VIX_ASYNC_ASIO_AWAIT_HPP

#include <coroutine>
#include <exception>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>

namespace vix::async::core
{
  class io_context;
}

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

  //  - T = void    : (std::error_code)
  //  - T != void   : (std::error_code, T)
  template <typename Starter, typename T>
  struct asio_awaitable
  {
    vix::async::core::io_context *ctx{};
    vix::async::core::cancel_token ct{};
    Starter starter;

    asio_result<T> res{};
    std::exception_ptr ex{};

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
      if (ct.is_cancelled())
      {
        ctx->post([h]() mutable
                  { if (h) h.resume(); });
        return;
      }

      try
      {
        if constexpr (std::is_void_v<T>)
        {
          starter([this, h](std::error_code ec) mutable
                  {
                    res.ec = ec;
                    ctx->post([h]() mutable { if (h) h.resume(); }); });
        }
        else
        {
          starter([this, h](std::error_code ec, T value) mutable
                  {
                    res.ec = ec;
                    if (!ec)
                      res.value.emplace(std::move(value));

                    ctx->post([h]() mutable { if (h) h.resume(); }); });
        }
      }
      catch (...)
      {
        ex = std::current_exception();
        ctx->post([h]() mutable
                  { if (h) h.resume(); });
      }
    }

    T await_resume()
    {
      if (ct.is_cancelled())
        throw std::system_error(vix::async::core::cancelled_ec());

      if (ex)
        std::rethrow_exception(ex);

      if (res.ec)
        throw to_system_error(res.ec);

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

#endif
