#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <future>
#include <span>
#include <string>

#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/core/when.hpp>
#include <vix/async/net/dns.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/async/net/udp.hpp>

using vix::async::core::io_context;
using vix::async::core::task;
using vix::async::core::when_all;

static constexpr std::uint16_t tcp_port = 39881;
static constexpr std::uint16_t udp_port = 39882;

static task<void> tcp_server(vix::async::net::tcp_listener &listener)
{
  auto stream = co_await listener.async_accept();
  std::array<std::byte, 16> buffer{};
  const auto read = co_await stream->async_read(buffer);
  assert(read == 4);

  std::size_t written = 0;
  while (written < read)
  {
    written += co_await stream->async_write(
        std::span<const std::byte>(buffer.data() + written, read - written));
  }

  stream->close();
}

static task<void> tcp_client(io_context &ctx)
{
  auto stream = vix::async::net::make_tcp_stream(ctx);
  co_await stream->async_connect({"127.0.0.1", tcp_port});

  const std::array<std::byte, 4> message{
      std::byte{'p'},
      std::byte{'i'},
      std::byte{'n'},
      std::byte{'g'}};

  std::size_t written = 0;
  while (written < message.size())
  {
    written += co_await stream->async_write(
        std::span<const std::byte>(
            message.data() + written,
            message.size() - written));
  }

  std::array<std::byte, 4> response{};
  std::size_t received = 0;
  while (received < response.size())
  {
    received += co_await stream->async_read(
        std::span<std::byte>(
            response.data() + received,
            response.size() - received));
  }

  assert(response == message);
  stream->close();
}

static task<void> tcp_smoke(io_context &ctx, std::promise<bool> &result)
{
  auto listener = vix::async::net::make_tcp_listener(ctx);
  co_await listener->async_listen({"127.0.0.1", tcp_port});

  co_await when_all(
      ctx.get_scheduler(),
      tcp_server(*listener),
      tcp_client(ctx));

  listener->close();
  result.set_value(true);
  ctx.stop();
}

static task<void> udp_smoke(io_context &ctx, std::promise<bool> &result)
{
  auto receiver = vix::async::net::make_udp_socket(ctx);
  auto sender = vix::async::net::make_udp_socket(ctx);

  co_await receiver->async_bind({"127.0.0.1", udp_port});
  co_await sender->async_bind({"127.0.0.1", 0});

  std::array<std::byte, 16> buffer{};
  const std::array<std::byte, 4> message{
      std::byte{'p'},
      std::byte{'o'},
      std::byte{'n'},
      std::byte{'g'}};

  auto values = co_await when_all(
      ctx.get_scheduler(),
      receiver->async_recv_from(buffer),
      sender->async_send_to(message, {"127.0.0.1", udp_port}));

  const auto &datagram = std::get<0>(values);
  const auto sent = std::get<1>(values);

  assert(datagram.bytes == message.size());
  assert(sent == message.size());
  assert(std::equal(
      message.begin(),
      message.end(),
      buffer.begin()));

  receiver->close();
  sender->close();
  result.set_value(true);
  ctx.stop();
}

static task<void> dns_smoke(io_context &ctx, std::promise<bool> &result)
{
  auto resolver = vix::async::net::make_dns_resolver(ctx);
  const auto addresses = co_await resolver->async_resolve("localhost", 80);
  result.set_value(!addresses.empty());
  ctx.stop();
}

int main()
{
  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();
    std::move(tcp_smoke(ctx, result)).start(ctx.get_scheduler());
    ctx.run();
    assert(future.get());
  }

  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();
    std::move(udp_smoke(ctx, result)).start(ctx.get_scheduler());
    ctx.run();
    assert(future.get());
  }

  {
    io_context ctx;
    std::promise<bool> result;
    auto future = result.get_future();
    std::move(dns_smoke(ctx, result)).start(ctx.get_scheduler());
    ctx.run();
    assert(future.get());
  }

  return 0;
}
