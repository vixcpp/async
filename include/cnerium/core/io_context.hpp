#pragma once

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
    io_context() = default;

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    scheduler &get_scheduler() noexcept { return sched_; }
    const scheduler &get_scheduler() const noexcept { return sched_; }

    cnerium::net::detail::asio_net_service &net();

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

    thread_pool &cpu_pool();
    timer &timers();
    signal_set &signals();

  private:
    scheduler sched_;
    std::unique_ptr<thread_pool> cpu_pool_;
    std::unique_ptr<timer> timer_;
    std::unique_ptr<signal_set> signals_;
    std::unique_ptr<cnerium::net::detail::asio_net_service> net_;
  };

} // namespace cnerium::core
