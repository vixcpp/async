/**
 *
 *  @file log.hpp
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
#ifndef VIX_ASYNC_LOG_HPP
#define VIX_ASYNC_LOG_HPP

#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string_view>

namespace vix::async::detail
{
  // Log levels
  enum class log_level : int
  {
    trace = 0,
    debug,
    info,
    warn,
    error,
    fatal,
    off
  };

  // Global log state
  inline std::atomic<log_level> g_log_level{log_level::info};
  inline std::mutex g_log_mutex;

  // Helpers
  inline const char *to_string(log_level lvl)
  {
    switch (lvl)
    {
    case log_level::trace:
      return "TRACE";
    case log_level::debug:
      return "DEBUG";
    case log_level::info:
      return "INFO";
    case log_level::warn:
      return "WARN";
    case log_level::error:
      return "ERROR";
    case log_level::fatal:
      return "FATAL";
    default:
      return "OFF";
    }
  }

  inline void set_log_level(log_level lvl) noexcept
  {
    g_log_level.store(lvl, std::memory_order_relaxed);
  }

  inline log_level get_log_level() noexcept
  {
    return g_log_level.load(std::memory_order_relaxed);
  }

  // ================================
  // Core log function
  // ================================
  inline void log(log_level lvl, std::string_view msg)
  {
    if (lvl < get_log_level())
      return;

    std::lock_guard<std::mutex> lock(g_log_mutex);

    // Timestamp (simple, localtime)
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);

    std::cerr << "[" << buf << "] "
              << "[" << to_string(lvl) << "] "
              << msg << "\n";

    if (lvl == log_level::fatal)
      std::abort();
  }

#define ASYNC_LOG_TRACE(msg) ::vix::async::detail::log(::vix::async::detail::log_level::trace, msg)
#define ASYNC_LOG_DEBUG(msg) ::vix::async::detail::log(::vix::async::detail::log_level::debug, msg)
#define ASYNC_LOG_INFO(msg) ::vix::async::detail::log(::vix::async::detail::log_level::info, msg)
#define ASYNC_LOG_WARN(msg) ::vix::async::detail::log(::vix::async::detail::log_level::warn, msg)
#define ASYNC_LOG_ERROR(msg) ::vix::async::detail::log(::vix::async::detail::log_level::error, msg)
#define ASYNC_LOG_FATAL(msg) ::vix::async::detail::log(::vix::async::detail::log_level::fatal, msg)

} // namespace vix::async::detail

#endif
