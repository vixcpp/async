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
 *  that can be found in the LICENSE file.
 *
 *  Vix.cpp
 *
 */
#include "asio_net_service.hpp"

#include <vix/async/core/io_context.hpp>

#include <utility>
#include <vector>

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
          }
        });
  }

  asio_net_service::operation_registration
  asio_net_service::register_operation(std::function<void()> cancel)
  {
    if (!cancel)
    {
      return {};
    }

    auto state = std::make_shared<operation_state>();
    state->cancel = std::move(cancel);

    bool invoke_now = false;

    {
      std::lock_guard<std::mutex> lock(operations_mutex_);

      if (stopped_.load(std::memory_order_acquire))
      {
        invoke_now = true;
      }
      else
      {
        operations_.push_back(state);
      }
    }

    operation_registration registration{state};

    if (invoke_now &&
        state->active.exchange(false, std::memory_order_acq_rel))
    {
      try
      {
        state->cancel();
      }
      catch (...)
      {
      }
    }

    return registration;
  }

  void asio_net_service::join() noexcept
  {
    if (!net_thread_.joinable())
    {
      return;
    }

    const auto self_id = std::this_thread::get_id();

    if (net_thread_.get_id() == self_id)
    {
      try
      {
        net_thread_.detach();
      }
      catch (...)
      {
      }
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

  asio_net_service::~asio_net_service()
  {
    stop();
    join();
  }

  void asio_net_service::stop() noexcept
  {
    if (stopped_.exchange(true, std::memory_order_acq_rel))
    {
      return;
    }

    std::vector<std::shared_ptr<operation_state>> operations;

    {
      std::lock_guard<std::mutex> lock(operations_mutex_);

      auto out = operations_.begin();
      for (auto it = operations_.begin(); it != operations_.end(); ++it)
      {
        if (auto operation = it->lock())
        {
          operations.push_back(operation);
          *out++ = *it;
        }
      }
      operations_.erase(out, operations_.end());
    }

    for (auto &operation : operations)
    {
      if (!operation ||
          !operation->active.exchange(false, std::memory_order_acq_rel))
      {
        continue;
      }

      try
      {
        if (operation->cancel)
        {
          operation->cancel();
        }
      }
      catch (...)
      {
      }
    }

    try
    {
      if (guard_)
      {
        guard_.reset();
      }
    }
    catch (...)
    {
    }
  }

} // namespace vix::async::net::detail
