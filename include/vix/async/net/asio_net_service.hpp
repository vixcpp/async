/**
 *
 *  @file asio_net_service.hpp
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
#ifndef VIX_ASYNC_ASIO_NET_SERVICE_HPP
#define VIX_ASYNC_ASIO_NET_SERVICE_HPP

#include <memory>
#include <thread>

#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>

namespace vix::async::core
{
  class io_context;
}

namespace vix::async::net::detail
{
  class asio_net_service
  {
  public:
    explicit asio_net_service(vix::async::core::io_context &ctx);
    ~asio_net_service();

    asio_net_service(const asio_net_service &) = delete;
    asio_net_service &operator=(const asio_net_service &) = delete;

    asio::io_context &asio_ctx() noexcept { return ioc_; }

    void stop() noexcept;

  private:
    vix::async::core::io_context &ctx_;
    asio::io_context ioc_;
    using guard_t = asio::executor_work_guard<asio::io_context::executor_type>;
    std::unique_ptr<guard_t> guard_;
    std::thread net_thread_;
    bool stopped_{false};
  };

} // namespace vix::async::net::detail

#endif
