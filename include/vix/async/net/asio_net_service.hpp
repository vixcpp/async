/**
 *
 *  @file asio_net_service.hpp
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
#ifndef VIX_ASYNC_ASIO_NET_SERVICE_HPP
#define VIX_ASYNC_ASIO_NET_SERVICE_HPP

#include <memory>
#include <thread>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace vix::async::core
{
  class io_context;
}
namespace vix::async::net::detail
{
  /**
   * @brief Internal Asio-backed networking service for the async runtime.
   *
   * asio_net_service hosts an independent asio::io_context running on a
   * dedicated network thread. It is designed as a lazy service owned by
   * vix::async::core::io_context and provides access to the underlying
   * asio::io_context for implementing async networking primitives.
   *
   * Lifetime model:
   * - Constructed with a reference to the core io_context (for integration)
   * - Uses a work guard to keep the Asio io_context alive
   * - Runs ioc_.run() on net_thread_
   * - stop() releases the guard and stops the Asio context
   */
  class asio_net_service
  {
  public:
    /**
     * @brief Construct the Asio networking service.
     *
     * Typically created lazily by vix::async::core::io_context::net().
     *
     * @param ctx Core io_context used by the runtime.
     */
    explicit asio_net_service(vix::async::core::io_context &ctx);

    /**
     * @brief Destroy the service.
     *
     * Ensures the network thread is stopped and joined.
     */
    ~asio_net_service();

    /**
     * @brief asio_net_service is non-copyable.
     */
    asio_net_service(const asio_net_service &) = delete;

    /**
     * @brief asio_net_service is non-copyable.
     */
    asio_net_service &operator=(const asio_net_service &) = delete;

    /**
     * @brief Access the underlying Asio io_context.
     *
     * @return Reference to asio::io_context.
     */
    asio::io_context &asio_ctx() noexcept { return ioc_; }

    /**
     * @brief Stop the networking service.
     *
     * Releases the work guard (if any), stops the Asio io_context,
     * and requests the network thread to exit.
     */
    void stop() noexcept;

    void join() noexcept;

  private:
    /**
     * @brief Bound core io_context.
     */
    vix::async::core::io_context &ctx_;

    /**
     * @brief Asio io_context used for networking operations.
     */
    asio::io_context ioc_;

    /**
     * @brief Work guard type used to keep asio_ctx() running.
     */
    using guard_t = asio::executor_work_guard<asio::io_context::executor_type>;

    /**
     * @brief Work guard instance (null when stopped).
     */
    std::unique_ptr<guard_t> guard_;

    /**
     * @brief Dedicated thread running the Asio event loop.
     */
    std::thread net_thread_;

    /**
     * @brief Indicates whether stop() has been requested.
     */
    bool stopped_{false};
  };

} // namespace vix::async::net::detail

#endif // VIX_ASYNC_ASIO_NET_SERVICE_HPP
