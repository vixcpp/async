#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <system_error>

#include <cnerium/core/task.hpp>
#include <cnerium/core/cancel.hpp>
#include <cnerium/core/error.hpp>

namespace cnerium::core
{
  class io_context;
}

namespace cnerium::net
{

  struct udp_endpoint
  {
    std::string host;
    std::uint16_t port{0};
  };

  struct udp_datagram
  {
    udp_endpoint from;
    std::size_t bytes{0};
  };

  // UDP socket contract: bind, send_to, recv_from.
  class udp_socket
  {
  public:
    virtual ~udp_socket() = default;
    virtual core::task<void> async_bind(const udp_endpoint &bind_ep) = 0;
    virtual core::task<std::size_t> async_send_to(
        std::span<const std::byte> buf,
        const udp_endpoint &to,
        core::cancel_token ct = {}) = 0;
    virtual core::task<udp_datagram> async_recv_from(
        std::span<std::byte> buf,
        core::cancel_token ct = {}) = 0;
    virtual void close() noexcept = 0;
    virtual bool is_open() const noexcept = 0;
  };

  std::unique_ptr<udp_socket> make_udp_socket(core::io_context &ctx);

} // namespace cnerium::net
