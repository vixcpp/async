/**
 *
 *  @file asio_udp.cpp
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
#include <vix/async/net/udp.hpp>
#include <vix/async/core/io_context.hpp>

#include "asio_net_service.hpp"
#include "asio_await.hpp"

#include <asio/ip/udp.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace vix::async::net
{
  using udp = asio::ip::udp;

  namespace detail
  {
    template <typename Starter, typename Cancel>
    inline vix::async::core::task<void> co_asio_void(
        core::io_context &ctx,
        core::cancel_token ct,
        Starter &&starter,
        Cancel &&cancel)
    {
      co_await asio_awaitable<std::decay_t<Starter>, void>{
          &ctx,
          &ctx.net(),
          std::move(ct),
          std::forward<Starter>(starter),
          std::function<void()>(std::forward<Cancel>(cancel))};
    }

    template <typename T, typename Starter, typename Cancel>
    inline vix::async::core::task<T> co_asio_value(
        core::io_context &ctx,
        core::cancel_token ct,
        Starter &&starter,
        Cancel &&cancel)
    {
      co_return co_await asio_awaitable<std::decay_t<Starter>, T>{
          &ctx,
          &ctx.net(),
          std::move(ct),
          std::forward<Starter>(starter),
          std::function<void()>(std::forward<Cancel>(cancel))};
    }
  } // namespace detail

  class udp_socket_asio final : public udp_socket
  {
  public:
    explicit udp_socket_asio(vix::async::core::io_context &ctx)
        : ctx_(ctx),
          service_(ctx_.net_shared()),
          sock_(std::make_shared<udp::socket>(service_->asio_ctx()))
    {
    }

    vix::async::core::task<void> async_bind(const udp_endpoint &bind_ep) override
    {
      udp::endpoint ep(asio::ip::make_address(bind_ep.host), bind_ep.port);

      std::error_code ec;

      sock_->open(ep.protocol(), ec);
      if (ec)
      {
        throw std::system_error(ec);
      }

      sock_->bind(ep, ec);
      if (ec)
      {
        throw std::system_error(ec);
      }

      co_return;
    }

    vix::async::core::task<std::size_t> async_send_to(
        std::span<const std::byte> buf,
        const udp_endpoint &to,
        core::cancel_token ct) override
    {
      udp::endpoint dst(asio::ip::make_address(to.host), to.port);

      co_return co_await detail::co_asio_value<std::size_t>(
          ctx_,
          ct,
          [&](auto done)
          {
            sock_->async_send_to(
                asio::buffer(buf.data(), buf.size()),
                dst,
                [done = std::move(done)](
                    std::error_code ec,
                    std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          },
          [sock = sock_]()
          {
            asio::post(
                sock->get_executor(),
                [sock]()
                {
                  std::error_code ec;
                  sock->cancel(ec);
                });
          });
    }

    vix::async::core::task<udp_datagram> async_recv_from(
        std::span<std::byte> buf,
        vix::async::core::cancel_token ct) override
    {
      udp::endpoint src;

      const auto received = co_await detail::co_asio_value<std::size_t>(
          ctx_,
          ct,
          [&](auto done)
          {
            sock_->async_receive_from(
                asio::buffer(buf.data(), buf.size()),
                src,
                [done = std::move(done)](
                    std::error_code ec,
                    std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          },
          [sock = sock_]()
          {
            asio::post(
                sock->get_executor(),
                [sock]()
                {
                  std::error_code ec;
                  sock->cancel(ec);
                });
          });

      udp_datagram d;
      d.from.host = src.address().to_string();
      d.from.port = src.port();
      d.bytes = received;

      co_return d;
    }

    void close() noexcept override
    {
      std::error_code ec;

      if (!sock_ || !sock_->is_open())
      {
        return;
      }

      sock_->cancel(ec);
      ec.clear();

      sock_->close(ec);
    }

    bool is_open() const noexcept override
    {
      return sock_ && sock_->is_open();
    }

  private:
    vix::async::core::io_context &ctx_;
    std::shared_ptr<detail::asio_net_service> service_;
    std::shared_ptr<udp::socket> sock_;
  };

  std::unique_ptr<udp_socket> make_udp_socket(vix::async::core::io_context &ctx)
  {
    return std::make_unique<udp_socket_asio>(ctx);
  }

} // namespace vix::async::net
