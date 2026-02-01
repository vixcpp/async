/**
 *
 *  @file io_context.cpp
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
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/signal.hpp>
#include <vix/async/core/thread_pool.hpp>
#include <vix/async/core/timer.hpp>
#include <vix/async/net/asio_net_service.hpp>

namespace vix::async::core
{
  io_context::io_context() = default;
  io_context::~io_context() = default;

  thread_pool &io_context::cpu_pool()
  {
    if (!cpu_pool_)
      cpu_pool_ = std::make_unique<thread_pool>(*this);
    return *cpu_pool_;
  }

  timer &io_context::timers()
  {
    if (!timer_)
      timer_ = std::make_unique<timer>(*this);
    return *timer_;
  }

  signal_set &io_context::signals()
  {
    if (!signals_)
      signals_ = std::make_unique<signal_set>(*this);
    return *signals_;
  }

  vix::async::net::detail::asio_net_service &io_context::net()
  {
    if (!net_)
      net_ = std::make_unique<vix::async::net::detail::asio_net_service>(*this);
    return *net_;
  }

} // namespace vix::async::core
