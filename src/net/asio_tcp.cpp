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

#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <atomic>

namespace vix::async::net
{
  using tcp = asio::ip::tcp;

  namespace detail
  {
    template <typename Starter>
    inline vix::async::core::task<void> co_asio_void(
        core::io_context &ctx,
        core::cancel_token ct,
        Starter &&starter)
    {
      co_await asio_awaitable<std::decay_t<Starter>, void>{
          &ctx,
          std::move(ct),
          std::forward<Starter>(starter)};
    }

    template <typename T, typename Starter>
    inline vix::async::core::task<T> co_asio_value(
        core::io_context &ctx,
        core::cancel_token ct,
        Starter &&starter)
    {
      co_return co_await asio_awaitable<std::decay_t<Starter>, T>{
          &ctx,
          std::move(ct),
          std::forward<Starter>(starter)};
    }
  } // namespace detail

  class tcp_stream_asio final : public tcp_stream
  {
  public:
    explicit tcp_stream_asio(vix::async::core::io_context &ctx)
        : ctx_(ctx),
          sock_(ctx_.net().asio_ctx())
    {
    }

    vix::async::core::task<void> async_connect(
        const tcp_endpoint &ep,
        vix::async::core::cancel_token ct) override
    {
      tcp::resolver resolver(ctx_.net().asio_ctx());

      auto results =
          co_await detail::co_asio_value<tcp::resolver::results_type>(
              ctx_,
              ct,
              [&](auto done)
              {
                resolver.async_resolve(
                    ep.host,
                    std::to_string(ep.port),
                    [done = std::move(done)](
                        std::error_code ec,
                        tcp::resolver::results_type r) mutable
                    {
                      done(ec, std::move(r));
                    });
              });

      co_await detail::co_asio_void(
          ctx_,
          ct,
          [&](auto done)
          {
            asio::async_connect(
                sock_,
                results,
                [done = std::move(done)](
                    std::error_code ec,
                    const tcp::endpoint &) mutable
                {
                  done(ec);
                });
          });

      co_return;
    }

    vix::async::core::task<std::size_t> async_read(
        std::span<std::byte> buf,
        vix::async::core::cancel_token ct) override
    {
      co_return co_await detail::co_asio_value<std::size_t>(
          ctx_,
          ct,
          [&](auto done)
          {
            sock_.async_read_some(
                asio::buffer(buf.data(), buf.size()),
                [done = std::move(done)](
                    std::error_code ec,
                    std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          });
    }

    vix::async::core::task<std::size_t> async_write(
        std::span<const std::byte> buf,
        vix::async::core::cancel_token ct) override
    {
      co_return co_await detail::co_asio_value<std::size_t>(
          ctx_,
          ct,
          [&](auto done)
          {
            asio::async_write(
                sock_,
                asio::buffer(buf.data(), buf.size()),
                [done = std::move(done)](
                    std::error_code ec,
                    std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          });
    }

    void close() noexcept override
    {
      std::error_code ec;

      if (!sock_.is_open())
      {
        return;
      }

      sock_.cancel(ec);
      ec.clear();

      sock_.shutdown(tcp::socket::shutdown_both, ec);
      ec.clear();

      sock_.close(ec);
    }

    bool is_open() const noexcept override
    {
      return sock_.is_open();
    }

    tcp::socket &native() noexcept
    {
      return sock_;
    }

    int native_handle() override
    {
      return static_cast<int>(sock_.native_handle());
    }

  private:
    core::io_context &ctx_;
    tcp::socket sock_;
  };

  class tcp_listener_asio final : public tcp_listener
  {
  public:
    explicit tcp_listener_asio(core::io_context &ctx)
        : ctx_(ctx),
          acc_(std::make_shared<tcp::acceptor>(ctx_.net().asio_ctx()))
    {
    }

    vix::async::core::task<void> async_listen(
        const tcp_endpoint &bind_ep,
        int backlog = 128) override
    {
      tcp::endpoint ep(asio::ip::make_address(bind_ep.host), bind_ep.port);

      std::error_code ec;

      acc_->open(ep.protocol(), ec);
      if (ec)
      {
        throw std::system_error(ec);
      }

      acc_->set_option(tcp::acceptor::reuse_address(true), ec);
      if (ec)
      {
        throw std::system_error(ec);
      }

      acc_->bind(ep, ec);
      if (ec)
      {
        throw std::system_error(ec);
      }

      acc_->listen(backlog, ec);
      if (ec)
      {
        throw std::system_error(ec);
      }

      open_.store(true, std::memory_order_release);
      closing_.store(false, std::memory_order_release);

      co_return;
    }

    vix::async::core::task<std::unique_ptr<tcp_stream>> async_accept(
        vix::async::core::cancel_token ct) override
    {
      if (ct.is_cancelled() || !open_.load(std::memory_order_acquire))
      {
        throw std::system_error(vix::async::core::cancelled_ec());
      }

      auto client = std::make_unique<tcp_stream_asio>(ctx_);
      auto acc = acc_;

      co_await detail::co_asio_void(
          ctx_,
          ct,
          [acc, client_ptr = client.get()](auto done)
          {
            acc->async_accept(
                client_ptr->native(),
                [acc, done = std::move(done)](std::error_code ec) mutable
                {
                  done(ec);
                });
          });

      co_return std::unique_ptr<tcp_stream>(client.release());
    }

    void close() noexcept override
    {
      open_.store(false, std::memory_order_release);

      if (closing_.exchange(true, std::memory_order_acq_rel))
      {
        return;
      }

      auto acc = acc_;

      try
      {
        asio::post(
            acc->get_executor(),
            [acc]() mutable
            {
              std::error_code ec;

              if (!acc->is_open())
              {
                return;
              }

              acc->cancel(ec);
              ec.clear();

              acc->close(ec);
            });
      }
      catch (...)
      {
        std::error_code ec;

        try
        {
          if (acc && acc->is_open())
          {
            acc->cancel(ec);
            ec.clear();

            acc->close(ec);
          }
        }
        catch (...)
        {
        }
      }
    }

    bool is_open() const noexcept override
    {
      return open_.load(std::memory_order_acquire);
    }

  private:
    vix::async::core::io_context &ctx_;
    std::shared_ptr<tcp::acceptor> acc_;
    std::atomic_bool open_{false};
    std::atomic_bool closing_{false};
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
