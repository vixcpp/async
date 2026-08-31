#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <system_error>
#include <thread>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/core/timer.hpp>

using namespace std::chrono_literals;
using vix::async::core::cancel_source;
using vix::async::core::errc;
using vix::async::core::io_context;
using vix::async::core::task;

static task<void> cancelled_sleep(
    io_context &ctx,
    vix::async::core::cancel_token token,
    std::promise<std::chrono::milliseconds> &result)
{
  const auto start = std::chrono::steady_clock::now();

  try
  {
    co_await ctx.timers().sleep_for(5s, token);
    assert(false && "sleep_for should have been cancelled");
  }
  catch (const std::system_error &error)
  {
    assert(error.code() == vix::async::core::make_error_code(errc::canceled));
  }

  result.set_value(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start));
  ctx.stop();
}

static task<void> stopped_sleep(
    io_context &ctx,
    std::promise<bool> &result)
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

  ctx.stop();
}

int main()
{
  {
    io_context ctx;
    std::atomic<int> completed{0};
    std::chrono::milliseconds early_elapsed{0};
    const auto start = std::chrono::steady_clock::now();

    ctx.timers().after(500ms, [&]()
                       {
      if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 == 2)
      {
        ctx.stop();
      } });

    std::thread inserter([&]()
                         {
      std::this_thread::sleep_for(50ms);
      ctx.timers().after(50ms, [&]()
                         {
        early_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 == 2)
        {
          ctx.stop();
        } }); });

    ctx.run();
    inserter.join();

    assert(completed.load(std::memory_order_acquire) == 2);
    assert(early_elapsed < 300ms);
  }

  {
    io_context ctx;
    cancel_source source;
    std::promise<std::chrono::milliseconds> result;
    auto future = result.get_future();

    std::move(cancelled_sleep(ctx, source.token(), result))
        .start(ctx.get_scheduler());

    std::thread canceller([&]()
                          {
      std::this_thread::sleep_for(50ms);
      source.request_cancel(); });

    ctx.run();
    canceller.join();

    assert(future.get() < 500ms);
  }

  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();

    std::move(stopped_sleep(ctx, result)).start(ctx.get_scheduler());

    std::thread stopper([&]()
                        {
      std::this_thread::sleep_for(50ms);
      ctx.timers().stop(); });

    ctx.run();
    stopper.join();

    assert(future.get());
  }

  return 0;
}
