#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <cnerium/core/task.hpp>
#include <cnerium/core/cancel.hpp>
#include <cnerium/core/error.hpp>

namespace cnerium::core
{
  class io_context;
}

namespace cnerium::net
{

  // A backend can return IP strings ("1.2.3.4", "::1", etc.).
  struct resolved_address
  {
    std::string ip;
    std::uint16_t port{0};
  };

  class dns_resolver
  {
  public:
    virtual ~dns_resolver() = default;

    // Resolve hostname + port into addresses.
    virtual core::task<std::vector<resolved_address>> async_resolve(
        std::string host,
        std::uint16_t port,
        core::cancel_token ct = {}) = 0;
  };

  std::unique_ptr<dns_resolver> make_dns_resolver(core::io_context &ctx);

} // namespace cnerium::net
