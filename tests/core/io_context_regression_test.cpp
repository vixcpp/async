#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <vix/async/core/error.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/core/timer.hpp>

using namespace std::chrono_literals;
using vix::async::core::errc;
using vix::async::core::io_context;
using vix::async::core::task;

static task<void> shutdown_sleep(io_context &ctx, std::promise<bool> &result)
{
  try
  {
    co_await ctx.timers().sleep_for(5s);
    result.set_value(false);
  }
  catch (const std::system_error &error)
  {
    result.set_value(
        error.code() == vix::async::core::make_error_code(errc::stopped));
  }
}

int main()
{
  {
    vix::async::core::scheduler scheduler;
    scheduler.post([]()
                   { throw std::runtime_error("fire-and-forget failure"); });
    scheduler.post([&scheduler]()
                   { scheduler.stop(); });
    scheduler.run();
    assert(!scheduler.is_running());
  }

  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();

    std::move(shutdown_sleep(ctx, result)).start(ctx.get_scheduler());

    std::thread shutdown_thread([&ctx]()
                                {
      std::this_thread::sleep_for(50ms);
      ctx.shutdown(); });

    ctx.run();
    shutdown_thread.join();
    assert(future.get());

    bool rejected = false;
    try
    {
      (void)ctx.timers();
    }
    catch (const std::runtime_error &)
    {
      rejected = true;
    }
    assert(rejected);
  }

  return 0;
}
