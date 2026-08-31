#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <system_error>
#include <thread>

#include <vix/async/core/error.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/core/thread_pool.hpp>

using namespace std::chrono_literals;
using vix::async::core::errc;
using vix::async::core::io_context;
using vix::async::core::task;
using vix::async::core::thread_pool;

static task<void> rejected_submit(io_context &ctx, std::promise<bool> &result)
{
  auto &pool = ctx.cpu_pool();
  pool.stop();

  try
  {
    (void)co_await pool.submit([]()
                              { return 42; });
    result.set_value(false);
  }
  catch (const std::system_error &error)
  {
    result.set_value(
        error.code() == vix::async::core::make_error_code(errc::rejected));
  }

  ctx.stop();
}

int main()
{
  {
    io_context ctx;
    thread_pool pool(ctx, 1);
    std::promise<void> ran;
    auto future = ran.get_future();

    const bool accepted = pool.post([&ran]()
                                    { ran.set_value(); });
    assert(accepted);
    assert(future.wait_for(1s) == std::future_status::ready);

    pool.stop();
    assert(!pool.post([]() {}));
  }

  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();

    std::move(rejected_submit(ctx, result)).start(ctx.get_scheduler());
    ctx.run();
    assert(future.get());
  }

  {
    io_context ctx;
    std::promise<void> destroyed;
    auto future = destroyed.get_future();

    std::promise<void> allow_destroy;
    auto destroy_gate = allow_destroy.get_future().share();

    auto *pool = new thread_pool(ctx, 1);
    const bool accepted = pool->post(
        [pool, &destroyed, destroy_gate]() mutable
        {
          // The owner releases this gate only after post() has returned.
          // Deleting an object while another thread is still inside one of its
          // member functions would itself be an invalid lifetime race.
          destroy_gate.wait();
          delete pool;
          destroyed.set_value();
        });

    assert(accepted);
    allow_destroy.set_value();
    assert(future.wait_for(2s) == std::future_status::ready);
  }

  return 0;
}
