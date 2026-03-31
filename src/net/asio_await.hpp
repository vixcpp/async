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
#include <vix/async/core/io_context.hpp>

namespace vix::async::net::detail
{
  /**
   * @brief Result container for Asio-backed async operations returning T.
   *
   * Stores the completion error code and the produced value, when present.
   *
   * @tparam T Result type.
   */
  template <typename T>
  struct asio_result
  {
    /**
     * @brief Completion error code.
     */
    std::error_code ec{};

    /**
     * @brief Result value, present only on success.
     */
    std::optional<T> value{};
  };

  /**
   * @brief Result container specialization for void operations.
   */
  template <>
  struct asio_result<void>
  {
    /**
     * @brief Completion error code.
     */
    std::error_code ec{};
  };

  /**
   * @brief Convert std::error_code to std::system_error.
   *
   * @param ec Error code.
   * @return Matching std::system_error.
   */
  inline std::system_error to_system_error(const std::error_code &ec)
  {
    return std::system_error(ec);
  }

  /**
   * @brief Resume a coroutine through the owning Vix async scheduler fast path.
   *
   * @param ctx Owning io_context.
   * @param h Coroutine handle to resume.
   */
  inline void resume_on_ctx(
      vix::async::core::io_context *ctx,
      std::coroutine_handle<> h) noexcept
  {
    if (!ctx || !h)
    {
      return;
    }

    ctx->post(h);
  }

  /**
   * @brief Coroutine awaitable bridging an Asio async operation into Vix task flow.
   *
   * Contract:
   * - T = void   => starter must invoke completion as: done(std::error_code)
   * - T != void  => starter must invoke completion as: done(std::error_code, T)
   *
   * Behavior:
   * - captures completion result or exception
   * - resumes awaiting coroutine through io_context fast coroutine path
   * - checks cancellation before and after suspension
   *
   * @tparam Starter Callable that starts the underlying Asio operation.
   * @tparam T Result type of the operation.
   */
  template <typename Starter, typename T>
  struct asio_awaitable
  {
    /**
     * @brief Owning io_context used for coroutine resumption.
     */
    vix::async::core::io_context *ctx{};

    /**
     * @brief Optional cancellation token.
     */
    vix::async::core::cancel_token ct{};

    /**
     * @brief Callable that starts the underlying Asio operation.
     */
    Starter starter;

    /**
     * @brief Stored completion result.
     */
    asio_result<T> res{};

    /**
     * @brief Stored exception thrown while starting the operation.
     */
    std::exception_ptr ex{};

    /**
     * @brief Always suspend to let Asio complete asynchronously.
     *
     * @return false
     */
    bool await_ready() const noexcept
    {
      return false;
    }

    /**
     * @brief Start the Asio operation and arrange coroutine resumption.
     *
     * @param h Awaiting coroutine handle.
     */
    void await_suspend(std::coroutine_handle<> h)
    {
      if (ct.is_cancelled())
      {
        resume_on_ctx(ctx, h);
        return;
      }

      try
      {
        if constexpr (std::is_void_v<T>)
        {
          starter(
              [this, h](std::error_code ec) mutable
              {
                res.ec = ec;
                resume_on_ctx(ctx, h);
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

                resume_on_ctx(ctx, h);
              });
        }
      }
      catch (...)
      {
        ex = std::current_exception();
        resume_on_ctx(ctx, h);
      }
    }

    /**
     * @brief Complete the await and return the Asio result.
     *
     * @return T for value-producing operations, void otherwise.
     * @throws std::system_error on cancellation or I/O failure.
     * @throws Rethrows any exception raised while starting the Asio operation.
     */
    T await_resume()
    {
      if (ct.is_cancelled())
      {
        throw std::system_error(vix::async::core::cancelled_ec());
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
