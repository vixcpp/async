/**
 *
 *  @file error.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
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
#include <type_traits>

namespace vix::async::core
{

  /**
   * @brief Error codes for the async core subsystem.
   *
   * This enumeration defines all error conditions that can be
   * reported by the asynchronous runtime, scheduler, thread pool,
   * timers, and cancellation mechanisms.
   *
   * Values are intentionally compact and stable to allow efficient
   * propagation through std::error_code.
   */
  enum class errc : std::uint8_t
  {
    /**
     * @brief No error.
     */
    ok = 0,

    // Generic

    /**
     * @brief Invalid argument passed to an API.
     */
    invalid_argument,

    /**
     * @brief Operation cannot complete yet.
     */
    not_ready,

    /**
     * @brief Operation timed out.
     */
    timeout,

    /**
     * @brief Operation was canceled.
     */
    canceled,

    /**
     * @brief Resource or channel was closed.
     */
    closed,

    /**
     * @brief Capacity or numeric overflow.
     */
    overflow,

    // Scheduler / runtime

    /**
     * @brief Runtime or scheduler has been stopped.
     */
    stopped,

    /**
     * @brief Internal task queue is full.
     */
    queue_full,

    // Thread pool

    /**
     * @brief Task submission was rejected.
     */
    rejected,

    // Signals / timers

    /**
     * @brief Operation is not supported on this platform.
     */
    not_supported
  };

  /**
   * @brief Error category for async core errors.
   *
   * Provides human-readable messages and categorization for
   * errc values. This category integrates with std::error_code
   * and std::error_condition.
   */
  class error_category final : public std::error_category
  {
  public:
    /**
     * @brief Return the name of the error category.
     *
     * @return Category name ("async").
     */
    [[nodiscard]] const char *name() const noexcept override
    {
      return "async";
    }

    /**
     * @brief Return a descriptive message for an error code.
     *
     * @param c Integer value of the error code.
     * @return Human-readable error message.
     */
    [[nodiscard]] std::string message(int c) const override
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

  /**
   * @brief Access the singleton async error category.
   *
   * @return Reference to the async error category.
   */
  [[nodiscard]] inline const std::error_category &category() noexcept
  {
    static error_category cat;
    return cat;
  }

  /**
   * @brief Create a std::error_code from an async error code.
   *
   * @param e Async error code.
   * @return Corresponding std::error_code.
   */
  [[nodiscard]] inline std::error_code make_error_code(errc e) noexcept
  {
    return {static_cast<int>(e), category()};
  }

} // namespace vix::async::core

namespace std
{
  /**
   * @brief Enable implicit conversion of errc to std::error_code.
   */
  template <>
  struct is_error_code_enum<vix::async::core::errc> : true_type
  {
  };
} // namespace std

#endif // VIX_ASYNC_ERROR_HPP
