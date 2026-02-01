#include <cnerium/net/dns.hpp>
#include <cnerium/core/io_context.hpp>

#include "asio_net_service.hpp"
#include "asio_await.hpp"

#include <asio/ip/tcp.hpp>

namespace cnerium::net
{

  class dns_resolver_asio final : public dns_resolver
  {
  public:
    explicit dns_resolver_asio(core::io_context &ctx)
        : ctx_(ctx),
          res_(ctx_.net().asio_ctx())
    {
    }

    core::task<std::vector<resolved_address>> async_resolve(std::string host, std::uint16_t port, core::cancel_token ct) override
    {
      auto results = co_await detail::asio_awaitable<
          std::function<void(std::function<void(std::error_code, asio::ip::tcp::resolver::results_type)>)>,
          asio::ip::tcp::resolver::results_type>{
          &ctx_, ct,
          [&](auto done)
          {
            res_.async_resolve(
                host, std::to_string(port),
                [done](std::error_code ec, asio::ip::tcp::resolver::results_type r) mutable
                {
                  done(ec, std::move(r));
                });
          }};

      std::vector<resolved_address> out;
      for (auto &e : results)
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

} // namespace cnerium::net
