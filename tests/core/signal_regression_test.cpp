#include <cassert>
#include <chrono>
#include <csignal>
#include <future>
#include <system_error>
#include <thread>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/signal.hpp>
#include <vix/async/core/task.hpp>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

using namespace std::chrono_literals;
using vix::async::core::cancel_source;
using vix::async::core::errc;
using vix::async::core::io_context;
using vix::async::core::task;

#if defined(__unix__) || defined(__APPLE__)
static task<void> wait_for_signal(io_context &ctx, std::promise<int> &result)
{
  const int signal = co_await ctx.signals().async_wait();
  result.set_value(signal);
  ctx.stop();
}

static task<void> wait_for_cancel(
    io_context &ctx,
    vix::async::core::cancel_token token,
    std::promise<bool> &result)
{
  try
  {
    (void)co_await ctx.signals().async_wait(token);
    result.set_value(false);
  }
  catch (const std::system_error &error)
  {
    result.set_value(
        error.code() == vix::async::core::make_error_code(errc::canceled));
  }

  ctx.stop();
}

static task<void> wait_for_stop(io_context &ctx, std::promise<bool> &result)
{
  try
  {
    (void)co_await ctx.signals().async_wait();
    result.set_value(false);
  }
  catch (const std::system_error &error)
  {
    result.set_value(
        error.code() == vix::async::core::make_error_code(errc::stopped));
  }

  ctx.stop();
}

static task<void> second_waiter(
    io_context &ctx,
    cancel_source &first_source,
    std::promise<bool> &result)
{
  co_await ctx.get_scheduler().schedule();

  try
  {
    (void)co_await ctx.signals().async_wait();
    result.set_value(false);
  }
  catch (const std::system_error &error)
  {
    result.set_value(
        error.code() == vix::async::core::make_error_code(errc::not_ready));
  }

  first_source.request_cancel();
  ctx.stop();
}

static task<void> first_waiter(
    io_context &ctx,
    vix::async::core::cancel_token token)
{
  try
  {
    (void)co_await ctx.signals().async_wait(token);
  }
  catch (...)
  {
  }
}
#endif

int main()
{
#if defined(__unix__) || defined(__APPLE__)
  {
    io_context ctx;
    ctx.signals().add(SIGUSR1);

    std::promise<int> result;
    auto future = result.get_future();
    std::move(wait_for_signal(ctx, result)).start(ctx.get_scheduler());

    std::thread sender([]()
                       {
      std::this_thread::sleep_for(50ms);
      ::kill(::getpid(), SIGUSR1); });

    ctx.run();
    sender.join();
    assert(future.get() == SIGUSR1);
  }

  {
    io_context ctx;
    ctx.signals().add(SIGUSR1);
    cancel_source source;
    std::promise<bool> result;
    auto future = result.get_future();

    std::move(wait_for_cancel(ctx, source.token(), result))
        .start(ctx.get_scheduler());

    std::thread canceller([&source]()
                          {
      std::this_thread::sleep_for(50ms);
      source.request_cancel(); });

    ctx.run();
    canceller.join();
    assert(future.get());
  }

  {
    io_context ctx;
    ctx.signals().add(SIGUSR1);
    std::promise<bool> result;
    auto future = result.get_future();

    std::move(wait_for_stop(ctx, result)).start(ctx.get_scheduler());

    std::thread stopper([&ctx]()
                        {
      std::this_thread::sleep_for(50ms);
      ctx.signals().stop(); });

    ctx.run();
    stopper.join();
    assert(future.get());
  }

  {
    io_context ctx;
    ctx.signals().add(SIGUSR1);
    cancel_source first_source;
    std::promise<bool> result;
    auto future = result.get_future();

    std::move(first_waiter(ctx, first_source.token()))
        .start(ctx.get_scheduler());
    std::move(second_waiter(ctx, first_source, result))
        .start(ctx.get_scheduler());

    ctx.run();
    assert(future.get());
  }
#endif

  return 0;
}
