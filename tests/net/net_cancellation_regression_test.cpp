#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <future>
#include <system_error>
#include <thread>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/async/net/udp.hpp>

using namespace std::chrono_literals;
using vix::async::core::cancel_source;
using vix::async::core::errc;
using vix::async::core::io_context;
using vix::async::core::task;

static constexpr std::uint16_t tcp_port = 39871;
static constexpr std::uint16_t udp_port = 39872;

static task<void> cancel_accept(io_context &ctx, std::promise<bool> &result)
{
  auto listener = vix::async::net::make_tcp_listener(ctx);
  co_await listener->async_listen({"127.0.0.1", tcp_port});

  cancel_source source;
  std::thread canceller([&source]()
                        {
    std::this_thread::sleep_for(50ms);
    source.request_cancel(); });

  try
  {
    (void)co_await listener->async_accept(source.token());
    result.set_value(false);
  }
  catch (const std::system_error &error)
  {
    result.set_value(
        error.code() == vix::async::core::make_error_code(errc::canceled));
  }

  canceller.join();
  listener->close();
  ctx.stop();
}

static task<void> cancel_udp_receive(io_context &ctx, std::promise<bool> &result)
{
  auto socket = vix::async::net::make_udp_socket(ctx);
  co_await socket->async_bind({"127.0.0.1", udp_port});

  cancel_source source;
  std::array<std::byte, 32> buffer{};

  std::thread canceller([&source]()
                        {
    std::this_thread::sleep_for(50ms);
    source.request_cancel(); });

  try
  {
    (void)co_await socket->async_recv_from(buffer, source.token());
    result.set_value(false);
  }
  catch (const std::system_error &error)
  {
    result.set_value(
        error.code() == vix::async::core::make_error_code(errc::canceled));
  }

  canceller.join();
  socket->close();
  ctx.stop();
}

static task<void> shutdown_active_accept(io_context &ctx, std::promise<bool> &result)
{
  auto listener = vix::async::net::make_tcp_listener(ctx);
  co_await listener->async_listen({"127.0.0.1", static_cast<std::uint16_t>(tcp_port + 2)});

  try
  {
    (void)co_await listener->async_accept();
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
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();
    std::move(cancel_accept(ctx, result)).start(ctx.get_scheduler());
    ctx.run();
    assert(future.get());
  }

  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();
    std::move(cancel_udp_receive(ctx, result)).start(ctx.get_scheduler());
    ctx.run();
    assert(future.get());
  }

  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();
    std::move(shutdown_active_accept(ctx, result)).start(ctx.get_scheduler());

    std::thread stopper([&ctx]()
                        {
      std::this_thread::sleep_for(50ms);
      ctx.shutdown(); });

    ctx.run();
    stopper.join();
    assert(future.get());
  }

  return 0;
}
