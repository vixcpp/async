/**
 *
 *  @file udp.hpp
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
#ifndef VIX_ASYNC_UDP_HPP
#define VIX_ASYNC_UDP_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <system_error>

#include <vix/async/core/task.hpp>
#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>

namespace vix::async::core
{
  class io_context;
}

namespace vix::async::net
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

} // namespace vix::async::net

#endif
