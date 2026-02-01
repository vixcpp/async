/**
 *
 *  @file asio_net_service.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/GaspardKirira/cnerium
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  CNERIUM
 *
 */
#include "asio_net_service.hpp"

#include <cnerium/core/io_context.hpp>

namespace cnerium::net::detail
{

  asio_net_service::asio_net_service(cnerium::core::io_context &ctx)
      : ctx_(ctx)
  {
    guard_ = std::make_unique<guard_t>(asio::make_work_guard(ioc_));

    net_thread_ = std::thread(
        [this]()
        { ioc_.run(); });
  }

  asio_net_service::~asio_net_service()
  {
    stop();
    if (net_thread_.joinable())
      net_thread_.join();
  }

  void asio_net_service::stop() noexcept
  {
    if (stopped_)
      return;
    stopped_ = true;

    if (guard_)
      guard_.reset();

    ioc_.stop();
  }

} // namespace cnerium::net::detail
