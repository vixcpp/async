/**
 *
 *  @file error.hpp
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
#ifndef VIX_ASYNC_ERROR_HPP
#define VIX_ASYNC_ERROR_HPP

#include <cstdint>
#include <string>
#include <system_error>

namespace vix::async::core
{

  enum class errc : std::uint8_t
  {
    ok = 0,

    // Generic
    invalid_argument,
    not_ready,
    timeout,
    canceled,
    closed,
    overflow,

    // Scheduler / runtime
    stopped,
    queue_full,

    // Thread pool
    rejected,

    // Signals / timers
    not_supported
  };

  class error_category final : public std::error_category
  {
  public:
    const char *name() const noexcept override
    {
      return "async";
    }

    std::string message(int c) const override
    {
      switch (static_cast<errc>(c))
      {
      case errc::ok:
        return "ok";
      case errc::invalid_argument:
        return "invalid argument";
      case errc::not_ready:
        return "not ready";
      case errc::timeout:
        return "timeout";
      case errc::canceled:
        return "canceled";
      case errc::closed:
        return "closed";
      case errc::overflow:
        return "overflow";
      case errc::stopped:
        return "stopped";
      case errc::queue_full:
        return "queue full";
      case errc::rejected:
        return "rejected";
      case errc::not_supported:
        return "not supported";
      default:
        return "unknown error";
      }
    }
  };

  inline const std::error_category &category() noexcept
  {
    static error_category cat;
    return cat;
  }

  inline std::error_code make_error_code(errc e) noexcept
  {
    return {static_cast<int>(e), category()};
  }

} // namespace async::core

namespace std
{
  template <>
  struct is_error_code_enum<vix::async::core::errc> : true_type
  {
  };
} // namespace std

#endif
