/**
 *
 *  @file io_context.hpp
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
#ifndef CNERIUM_IO_CONTEXT_HPP
#define CNERIUM_IO_CONTEXT_HPP

#include <coroutine>
#include <memory>
#include <utility>

#include <cnerium/core/scheduler.hpp>

namespace cnerium::net::detail
{
  class asio_net_service;
}

namespace cnerium::core
{
  class thread_pool;
  class timer;
  class signal_set;

  class io_context
  {
  public:
    io_context();
    ~io_context();

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    scheduler &get_scheduler() noexcept { return sched_; }
    const scheduler &get_scheduler() const noexcept { return sched_; }

    template <typename Fn>
    void post(Fn &&fn)
    {
      sched_.post(std::forward<Fn>(fn));
    }

    void post(std::coroutine_handle<> h)
    {
      sched_.post(h);
    }

    void run()
    {
      sched_.run();
    }

    void stop() noexcept
    {
      sched_.stop();
    }

    bool is_running() const noexcept
    {
      return sched_.is_running();
    }

    // Lazy services
    thread_pool &cpu_pool();
    timer &timers();
    signal_set &signals();
    cnerium::net::detail::asio_net_service &net();

  private:
    scheduler sched_;
    std::unique_ptr<thread_pool> cpu_pool_;
    std::unique_ptr<timer> timer_;
    std::unique_ptr<signal_set> signals_;
    std::unique_ptr<cnerium::net::detail::asio_net_service> net_;
  };

} // namespace cnerium::core

#endif
