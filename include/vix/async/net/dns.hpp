/**
 *
 *  @file dns.hpp
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
#ifndef VIX_ASYNC_DNS_HPP
#define VIX_ASYNC_DNS_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vix/async/core/task.hpp>
#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>

namespace vix::async::core
{
  class io_context;
}

namespace vix::async::net
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

} // namespace vix::async::net

#endif
