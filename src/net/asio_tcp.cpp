/**
 *
 *  @file asio_tcp.cpp
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
#include <vix/async/net/tcp.hpp>
#include <vix/async/core/io_context.hpp>

#include "asio_net_service.hpp"
#include "asio_await.hpp"

#include <asio/ip/tcp.hpp>
#include <asio/connect.hpp>
#include <asio/write.hpp>
#include <asio/read.hpp>

namespace vix::async::net
{
  using tcp = asio::ip::tcp;

  class tcp_stream_asio final : public tcp_stream
  {
  public:
    explicit tcp_stream_asio(vix::async::core::io_context &ctx)
        : ctx_(ctx),
          sock_(ctx_.net().asio_ctx())
    {
    }

    vix::async::core::task<void> async_connect(const tcp_endpoint &ep, vix::async::core::cancel_token ct) override
    {
      tcp::resolver resolver(ctx_.net().asio_ctx());

      auto results = co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code, tcp::resolver::results_type)>)>,
          tcp::resolver::results_type>{
          &ctx_, ct,
          [&](auto done)
          {
            resolver.async_resolve(
                ep.host, std::to_string(ep.port),
                [done](std::error_code ec, tcp::resolver::results_type r) mutable
                {
                  done(ec, std::move(r));
                });
          }};

      co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code)>)>,
          void>{
          &ctx_, ct,
          [&](auto done)
          {
            asio::async_connect(
                sock_, results,
                [done](std::error_code ec, const tcp::endpoint &) mutable
                {
                  done(ec);
                });
          }};

      co_return;
    }

    vix::async::core::task<std::size_t> async_read(std::span<std::byte> buf, vix::async::core::cancel_token ct) override
    {
      auto bytes_read = co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code, std::size_t)>)>,
          std::size_t>{
          &ctx_, ct,
          [&](auto done)
          {
            sock_.async_read_some(
                asio::buffer(buf.data(), buf.size()),
                [done](std::error_code ec, std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          }};

      co_return bytes_read;
    }

    vix::async::core::task<std::size_t> async_write(std::span<const std::byte> buf, vix::async::core::cancel_token ct) override
    {
      auto bytes_written = co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code, std::size_t)>)>,
          std::size_t>{
          &ctx_, ct,
          [&](auto done)
          {
            asio::async_write(
                sock_, asio::buffer(buf.data(), buf.size()),
                [done](std::error_code ec, std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          }};

      co_return bytes_written;
    }

    void close() noexcept override
    {
      std::error_code ec;
      sock_.close(ec);
    }

    bool is_open() const noexcept override
    {
      return sock_.is_open();
    }

    tcp::socket &native() noexcept { return sock_; }

  private:
    core::io_context &ctx_;
    tcp::socket sock_;
  };

  class tcp_listener_asio final : public tcp_listener
  {
  public:
    explicit tcp_listener_asio(core::io_context &ctx)
        : ctx_(ctx),
          acc_(ctx_.net().asio_ctx())
    {
    }

    vix::async::core::task<void> async_listen(const tcp_endpoint &bind_ep, int backlog) override
    {
      tcp::endpoint ep(asio::ip::make_address(bind_ep.host), bind_ep.port);

      std::error_code ec;
      acc_.open(ep.protocol(), ec);
      if (ec)
        throw std::system_error(ec);

      acc_.set_option(tcp::acceptor::reuse_address(true), ec);
      if (ec)
        throw std::system_error(ec);

      acc_.bind(ep, ec);
      if (ec)
        throw std::system_error(ec);

      acc_.listen(backlog, ec);
      if (ec)
        throw std::system_error(ec);

      co_return;
    }

    vix::async::core::task<std::unique_ptr<tcp_stream>> async_accept(vix::async::core::cancel_token ct) override
    {
      auto client = std::make_unique<tcp_stream_asio>(ctx_);

      co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code)>)>,
          void>{
          &ctx_, ct,
          [&](auto done)
          {
            acc_.async_accept(
                client->native(),
                [done](std::error_code ec) mutable
                { done(ec); });
          }};

      co_return std::unique_ptr<tcp_stream>(client.release());
    }

    void close() noexcept override
    {
      std::error_code ec;
      acc_.close(ec);
    }

    bool is_open() const noexcept override
    {
      return acc_.is_open();
    }

  private:
    vix::async::core::io_context &ctx_;
    tcp::acceptor acc_;
  };

  std::unique_ptr<tcp_stream> make_tcp_stream(vix::async::core::io_context &ctx)
  {
    return std::make_unique<tcp_stream_asio>(ctx);
  }

  std::unique_ptr<tcp_listener> make_tcp_listener(vix::async::core::io_context &ctx)
  {
    return std::make_unique<tcp_listener_asio>(ctx);
  }

} // namespace vix::async::net
