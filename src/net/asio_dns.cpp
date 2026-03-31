/**
 *
 *  @file asio_dns.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/async/net/dns.hpp>
#include <vix/async/core/io_context.hpp>

#include "asio_net_service.hpp"
#include "asio_await.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

#include <asio/ip/tcp.hpp>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vix::async::net
{
  namespace detail
  {
    template <typename Starter>
    inline vix::async::core::task<void> co_asio_void(
        core::io_context &ctx,
        core::cancel_token ct,
        Starter &&starter)
    {
      co_await asio_awaitable<std::decay_t<Starter>, void>{
          &ctx,
          std::move(ct),
          std::forward<Starter>(starter)};
    }

    template <typename T, typename Starter>
    inline vix::async::core::task<T> co_asio_value(
        core::io_context &ctx,
        core::cancel_token ct,
        Starter &&starter)
    {
      co_return co_await asio_awaitable<std::decay_t<Starter>, T>{
          &ctx,
          std::move(ct),
          std::forward<Starter>(starter)};
    }
  } // namespace detail

  class dns_resolver_asio final : public dns_resolver
  {
  public:
    explicit dns_resolver_asio(vix::async::core::io_context &ctx)
        : ctx_(ctx),
          res_(ctx_.net().asio_ctx())
    {
    }

    core::task<std::vector<resolved_address>> async_resolve(
        std::string host,
        std::uint16_t port,
        core::cancel_token ct) override
    {
      const auto results =
          co_await detail::co_asio_value<asio::ip::tcp::resolver::results_type>(
              ctx_,
              ct,
              [&](auto done)
              {
                res_.async_resolve(
                    host,
                    std::to_string(port),
                    [done = std::move(done)](
                        std::error_code ec,
                        asio::ip::tcp::resolver::results_type r) mutable
                    {
                      done(ec, std::move(r));
                    });
              });

      std::vector<resolved_address> out;
      out.reserve(results.size());

      for (const auto &e : results)
      {
        resolved_address a;
        a.ip = e.endpoint().address().to_string();
        a.port = e.endpoint().port();
        out.push_back(std::move(a));
      }

      co_return out;
    }

  private:
    core::io_context &ctx_;
    asio::ip::tcp::resolver res_;
  };

  std::unique_ptr<dns_resolver> make_dns_resolver(core::io_context &ctx)
  {
    return std::make_unique<dns_resolver_asio>(ctx);
  }

} // namespace vix::async::net
