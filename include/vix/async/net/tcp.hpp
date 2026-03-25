/**
 *
 *  @file tcp.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_ASYNC_TCP_HPP
#define VIX_ASYNC_TCP_HPP

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
  /**
   * @brief TCP endpoint description.
   *
   * Represents a network endpoint defined by a hostname (or IP string)
   * and a TCP port.
   */
  struct tcp_endpoint
  {
    /**
     * @brief Hostname or IP address.
     *
     * Examples: "example.com", "127.0.0.1", "::1".
     */
    std::string host;

    /**
     * @brief TCP port number in host byte order.
     */
    std::uint16_t port{0};
  };

  /**
   * @brief Abstract asynchronous TCP stream interface.
   *
   * tcp_stream represents a connected TCP socket and exposes coroutine-based
   * asynchronous operations for connecting, reading, and writing.
   *
   * Implementations are runtime-specific (typically backed by Asio) and
   * integrate with vix::async::core::io_context.
   */
  class tcp_stream
  {
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~tcp_stream() = default;

    /**
     * @brief Asynchronously connect to a remote TCP endpoint.
     *
     * @param ep Remote endpoint to connect to.
     * @param ct Optional cancellation token.
     *
     * @return task<void> that completes once the connection is established.
     *
     * @throws std::system_error on connection failure or cancellation.
     */
    virtual core::task<void> async_connect(
        const tcp_endpoint &ep,
        core::cancel_token ct = {}) = 0;

    /**
     * @brief Asynchronously read data from the stream.
     *
     * Reads up to buf.size() bytes into the provided buffer.
     *
     * @param buf Destination buffer.
     * @param ct Optional cancellation token.
     *
     * @return task<std::size_t> Number of bytes actually read.
     *
     * @throws std::system_error on read failure or cancellation.
     */
    virtual core::task<std::size_t> async_read(
        std::span<std::byte> buf,
        core::cancel_token ct = {}) = 0;

    /**
     * @brief Asynchronously write data to the stream.
     *
     * Writes the contents of the provided buffer to the TCP connection.
     *
     * @param buf Source buffer.
     * @param ct Optional cancellation token.
     *
     * @return task<std::size_t> Number of bytes actually written.
     *
     * @throws std::system_error on write failure or cancellation.
     */
    virtual core::task<std::size_t> async_write(
        std::span<const std::byte> buf,
        core::cancel_token ct = {}) = 0;

    /**
     * @brief Close the TCP stream.
     *
     * This operation is idempotent and may be called multiple times.
     */
    virtual void close() noexcept = 0;

    /**
     * @brief Check whether the stream is currently open.
     *
     * @return true if the stream is open, false otherwise.
     */
    virtual bool is_open() const noexcept = 0;
  };

  /**
   * @brief Abstract asynchronous TCP listener interface.
   *
   * tcp_listener represents a listening TCP socket that can accept incoming
   * connections asynchronously and produce tcp_stream instances.
   */
  class tcp_listener
  {
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~tcp_listener() = default;

    /**
     * @brief Bind and start listening on a TCP endpoint.
     *
     * This operation is synchronous and returns only once the listener
     * is effectively ready to accept incoming connections.
     *
     * @param bind_ep Local endpoint to bind to.
     * @param backlog Maximum pending connection backlog.
     *
     * @throws std::system_error on bind or listen failure.
     */
    virtual void listen(
        const tcp_endpoint &bind_ep,
        int backlog = 128) = 0;

    /**
     * @brief Asynchronously accept a new incoming connection.
     *
     * @param ct Optional cancellation token.
     *
     * @return task<std::unique_ptr<tcp_stream>> Newly accepted TCP stream.
     *
     * @throws std::system_error on accept failure or cancellation.
     */
    virtual core::task<std::unique_ptr<tcp_stream>> async_accept(
        core::cancel_token ct = {}) = 0;

    /**
     * @brief Close the TCP listener.
     *
     * This stops accepting new connections and releases the underlying socket.
     */
    virtual void close() noexcept = 0;

    /**
     * @brief Check whether the listener is currently open.
     *
     * @return true if the listener is open, false otherwise.
     */
    virtual bool is_open() const noexcept = 0;
  };

  /**
   * @brief Create a TCP stream associated with an io_context.
   *
   * @param ctx Core io_context used for scheduling and integration.
   * @return Unique pointer owning a tcp_stream instance.
   */
  std::unique_ptr<tcp_stream> make_tcp_stream(core::io_context &ctx);

  /**
   * @brief Create a TCP listener associated with an io_context.
   *
   * @param ctx Core io_context used for scheduling and integration.
   * @return Unique pointer owning a tcp_listener instance.
   */
  std::unique_ptr<tcp_listener> make_tcp_listener(core::io_context &ctx);

} // namespace vix::async::net

#endif // VIX_ASYNC_TCP_HPP
