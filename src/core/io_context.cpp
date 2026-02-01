#include <cnerium/core/io_context.hpp>

#include <cnerium/core/signal.hpp>
#include <cnerium/core/thread_pool.hpp>
#include <cnerium/core/timer.hpp>
#include <cnerium/net/asio_net_service.hpp>

namespace cnerium::core
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

  cnerium::net::detail::asio_net_service &io_context::net()
  {
    if (!net_)
      net_ = std::make_unique<cnerium::net::detail::asio_net_service>(*this);
    return *net_;
  }

} // namespace cnerium::core
