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

  struct tcp_endpoint
  {
    std::string host;
    std::uint16_t port{0};
  };

  class tcp_stream
  {
  public:
    virtual ~tcp_stream() = default;
    virtual core::task<void> async_connect(const tcp_endpoint &ep, core::cancel_token ct = {}) = 0;
    virtual core::task<std::size_t> async_read(std::span<std::byte> buf, core::cancel_token ct = {}) = 0;
    virtual core::task<std::size_t> async_write(std::span<const std::byte> buf, core::cancel_token ct = {}) = 0;
    virtual void close() noexcept = 0;
    virtual bool is_open() const noexcept = 0;
  };

  class tcp_listener
  {
  public:
    virtual ~tcp_listener() = default;
    virtual core::task<void> async_listen(const tcp_endpoint &bind_ep, int backlog = 128) = 0;
    virtual core::task<std::unique_ptr<tcp_stream>> async_accept(core::cancel_token ct = {}) = 0;
    virtual void close() noexcept = 0;
    virtual bool is_open() const noexcept = 0;
  };

  std::unique_ptr<tcp_stream> make_tcp_stream(core::io_context &ctx);
  std::unique_ptr<tcp_listener> make_tcp_listener(core::io_context &ctx);

} // namespace cnerium::net
