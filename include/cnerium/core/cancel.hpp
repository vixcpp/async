/**
 *
 *  @file cancel.hpp
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
#pragma once

#include <atomic>
#include <memory>

#include <cnerium/core/error.hpp>

namespace cnerium::core
{
  class cancel_state
  {
  public:
    void request_cancel() noexcept
    {
      cancelled_.store(true, std::memory_order_release);
    }

    bool is_cancelled() const noexcept
    {
      return cancelled_.load(std::memory_order_acquire);
    }

  private:
    std::atomic<bool> cancelled_{false};
  };

  class cancel_token
  {
  public:
    cancel_token() = default;

    explicit cancel_token(std::shared_ptr<cancel_state> st) noexcept
        : st_(std::move(st)) {}

    bool can_cancel() const noexcept { return static_cast<bool>(st_); }

    bool is_cancelled() const noexcept
    {
      return st_ ? st_->is_cancelled() : false;
    }

  private:
    std::shared_ptr<cancel_state> st_{};
  };

  class cancel_source
  {
  public:
    cancel_source() : st_(std::make_shared<cancel_state>()) {}

    cancel_token token() const noexcept
    {
      return cancel_token{st_};
    }

    void request_cancel() noexcept
    {
      if (st_)
        st_->request_cancel();
    }

    bool is_cancelled() const noexcept
    {
      return st_ ? st_->is_cancelled() : false;
    }

  private:
    std::shared_ptr<cancel_state> st_;
  };

  // Helper: translate cancellation to error_code (uniform behavior).
  inline std::error_code cancelled_ec() noexcept
  {
    return make_error_code(errc::canceled);
  }

} // namespace cnerium::core
