#include <cnerium/net/udp.hpp>
#include <cnerium/core/io_context.hpp>

#include "asio_net_service.hpp"
#include "asio_await.hpp"

#include <asio/ip/udp.hpp>

namespace cnerium::net
{
  using udp = asio::ip::udp;

  class udp_socket_asio final : public udp_socket
  {
  public:
    explicit udp_socket_asio(core::io_context &ctx)
        : ctx_(ctx),
          sock_(ctx_.net().asio_ctx())
    {
    }

    core::task<void> async_bind(const udp_endpoint &bind_ep) override
    {
      udp::endpoint ep(asio::ip::make_address(bind_ep.host), bind_ep.port);

      std::error_code ec;
      sock_.open(ep.protocol(), ec);
      if (ec)
        throw std::system_error(ec);

      sock_.bind(ep, ec);
      if (ec)
        throw std::system_error(ec);

      co_return;
    }

    core::task<std::size_t> async_send_to(
        std::span<const std::byte> buf,
        const udp_endpoint &to,
        core::cancel_token ct) override
    {
      udp::endpoint dst(asio::ip::make_address(to.host), to.port);

      auto sent = co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code, std::size_t)>)>,
          std::size_t>{
          &ctx_, ct,
          [&](auto done)
          {
            sock_.async_send_to(
                asio::buffer(buf.data(), buf.size()), dst,
                [done](std::error_code ec, std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          }};

      co_return sent;
    }

    core::task<udp_datagram> async_recv_from(
        std::span<std::byte> buf,
        core::cancel_token ct) override
    {
      udp::endpoint src;

      auto received = co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code, std::size_t)>)>,
          std::size_t>{
          &ctx_, ct,
          [&](auto done)
          {
            sock_.async_receive_from(
                asio::buffer(buf.data(), buf.size()), src,
                [done](std::error_code ec, std::size_t bytes) mutable
                {
                  done(ec, bytes);
                });
          }};

      udp_datagram d;
      d.from.host = src.address().to_string();
      d.from.port = src.port();
      d.bytes = received;

      co_return d;
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

  private:
    core::io_context &ctx_;
    udp::socket sock_;
  };

  std::unique_ptr<udp_socket> make_udp_socket(core::io_context &ctx)
  {
    return std::make_unique<udp_socket_asio>(ctx);
  }

} // namespace cnerium::net
