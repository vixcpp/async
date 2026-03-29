/**
 *
 *  @file asio_net_service.cpp
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
#include "asio_net_service.hpp"

#include <vix/async/core/io_context.hpp>

namespace vix::async::net::detail
{

  asio_net_service::asio_net_service(vix::async::core::io_context &)
  {
    guard_ = std::make_unique<guard_t>(asio::make_work_guard(ioc_));

    net_thread_ = std::thread(
        [this]()
        {
          try
          {
            ioc_.run();
          }
          catch (...)
          {
            // never propagate exceptions out of thread
          }
        });
  }

  asio_net_service::~asio_net_service()
  {
    stop();

    if (!net_thread_.joinable())
      return;

    const auto self_id = std::this_thread::get_id();

    if (net_thread_.get_id() == self_id)
    {
      net_thread_.detach();
      return;
    }

    try
    {
      net_thread_.join();
    }
    catch (...)
    {
      try
      {
        net_thread_.detach();
      }
      catch (...)
      {
      }
    }
  }

  void asio_net_service::stop() noexcept
  {
    if (stopped_)
      return;

    stopped_ = true;

    try
    {
      if (guard_)
        guard_.reset();
    }
    catch (...)
    {
    }

    try
    {
      ioc_.stop();
    }
    catch (...)
    {
    }
  }

} // namespace vix::async::net::detail
