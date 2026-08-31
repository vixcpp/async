/**
 *
 *  @file cancel.hpp
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
#ifndef VIX_ASYNC_CANCEL_HPP
#define VIX_ASYNC_CANCEL_HPP

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <thread>

#include <vix/async/core/error.hpp>

namespace vix::async::core
{
  namespace detail
  {
    struct cancel_callback_state
    {
      mutable std::mutex mutex{};
      std::condition_variable cv{};
      bool active{true};
      bool running{false};
      std::thread::id running_thread{};
      std::function<void()> fn{};

      bool begin() noexcept
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!active)
        {
          return false;
        }

        active = false;
        running = true;
        running_thread = std::this_thread::get_id();
        return true;
      }

      void finish() noexcept
      {
        {
          std::lock_guard<std::mutex> lock(mutex);
          running = false;
          running_thread = {};
        }
        cv.notify_all();
      }

      void deactivate() noexcept
      {
        std::unique_lock<std::mutex> lock(mutex);
        active = false;

        if (running && running_thread != std::this_thread::get_id())
        {
          cv.wait(lock, [this]()
                  { return !running; });
        }
      }

      bool is_active() const noexcept
      {
        std::lock_guard<std::mutex> lock(mutex);
        return active;
      }
    };
  } // namespace detail

  /**
   * @brief RAII registration for one cancellation callback.
   *
   * Destroying or resetting the registration prevents a callback that has not
   * already started from being invoked by a later cancellation request.
   */
  class cancel_registration
  {
  public:
    cancel_registration() noexcept = default;

    explicit cancel_registration(
        std::shared_ptr<detail::cancel_callback_state> state) noexcept
        : state_(std::move(state))
    {
    }

    cancel_registration(cancel_registration &&other) noexcept
        : state_(std::move(other.state_))
    {
    }

    cancel_registration &operator=(cancel_registration &&other) noexcept
    {
      if (this != &other)
      {
        reset();
        state_ = std::move(other.state_);
      }
      return *this;
    }

    cancel_registration(const cancel_registration &) = delete;
    cancel_registration &operator=(const cancel_registration &) = delete;

    ~cancel_registration()
    {
      reset();
    }

    void reset() noexcept
    {
      if (state_)
      {
        state_->deactivate();
        state_.reset();
      }
    }

    [[nodiscard]] bool active() const noexcept
    {
      return state_ && state_->is_active();
    }

  private:
    std::shared_ptr<detail::cancel_callback_state> state_{};
  };

  /**
   * @brief Shared cancellation state.
   *
   * cancel_state holds the atomic cancellation flag shared between
   * a cancel_source and all associated cancel_token instances.
   *
   * This object is reference-counted and designed to be safely
   * accessed concurrently from multiple threads.
   */
  class cancel_state
  {
  public:
    /**
     * @brief Request cancellation.
     *
     * Sets the internal cancellation flag. This operation is
     * thread-safe and may be called multiple times.
     */
    void request_cancel() noexcept
    {
      if (cancelled_.exchange(true, std::memory_order_acq_rel))
      {
        return;
      }

      std::vector<std::shared_ptr<detail::cancel_callback_state>> callbacks;

      {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);

        auto out = callbacks_.begin();
        for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it)
        {
          if (auto callback = it->lock())
          {
            callbacks.push_back(callback);
            *out++ = *it;
          }
        }
        callbacks_.erase(out, callbacks_.end());
      }

      for (auto &callback : callbacks)
      {
        if (!callback || !callback->begin())
        {
          continue;
        }

        try
        {
          if (callback->fn)
          {
            callback->fn();
          }
        }
        catch (...)
        {
        }

        callback->finish();
      }
    }

    /**
     * @brief Check whether cancellation was requested.
     *
     * @return true if cancellation has been requested, false otherwise.
     */
    bool is_cancelled() const noexcept
    {
      return cancelled_.load(std::memory_order_acquire);
    }

    cancel_registration subscribe(std::function<void()> fn)
    {
      if (!fn)
      {
        return {};
      }

      auto callback = std::make_shared<detail::cancel_callback_state>();
      callback->fn = std::move(fn);

      bool invoke_now = false;

      {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);

        if (cancelled_.load(std::memory_order_acquire))
        {
          invoke_now = true;
        }
        else
        {
          callbacks_.erase(
              std::remove_if(
                  callbacks_.begin(),
                  callbacks_.end(),
                  [](const auto &entry)
                  {
                    return entry.expired();
                  }),
              callbacks_.end());
          callbacks_.push_back(callback);
        }
      }

      cancel_registration registration{callback};

      if (invoke_now && callback->begin())
      {
        try
        {
          callback->fn();
        }
        catch (...)
        {
        }


        callback->finish();
      }

      return registration;
    }

  private:
    /**
     * @brief Atomic cancellation flag.
     */
    std::atomic<bool> cancelled_{false};

    mutable std::mutex callbacks_mutex_;
    std::vector<std::weak_ptr<detail::cancel_callback_state>> callbacks_;
  };

  /**
   * @brief Lightweight cancellation observer.
   *
   * cancel_token provides a read-only view of a cancellation state.
   * It does not own the state and cannot request cancellation itself.
   *
   * Tokens are cheap to copy and may be safely passed across threads.
   */
  class cancel_token
  {
  public:
    /**
     * @brief Construct an empty (non-cancelable) token.
     */
    cancel_token() = default;

    /**
     * @brief Construct a token bound to a cancellation state.
     *
     * @param st Shared cancellation state.
     */
    explicit cancel_token(std::shared_ptr<cancel_state> st) noexcept
        : st_(std::move(st)) {}

    /**
     * @brief Check whether this token is associated with a cancel source.
     *
     * @return true if the token can observe cancellation, false otherwise.
     */
    bool can_cancel() const noexcept
    {
      return static_cast<bool>(st_);
    }

    /**
     * @brief Check whether cancellation has been requested.
     *
     * @return true if cancellation was requested, false otherwise.
     */
    bool is_cancelled() const noexcept
    {
      return st_ ? st_->is_cancelled() : false;
    }

    /**
     * @brief Register a callback invoked once when cancellation is requested.
     *
     * If cancellation was already requested, the callback is invoked during
     * this call. The returned registration disables future invocation when it
     * is destroyed or reset.
     */
    cancel_registration on_cancel(std::function<void()> fn) const
    {
      if (!st_)
      {
        return {};
      }

      return st_->subscribe(std::move(fn));
    }

  private:
    /**
     * @brief Shared cancellation state.
     */
    std::shared_ptr<cancel_state> st_{};
  };

  /**
   * @brief Cancellation source and owner.
   *
   * cancel_source owns the cancellation state and is responsible
   * for issuing cancellation requests. All tokens produced by
   * this source observe the same cancellation state.
   */
  class cancel_source
  {
  public:
    /**
     * @brief Construct a new cancellation source.
     *
     * The associated cancellation state starts in a non-cancelled state.
     */
    cancel_source()
        : st_(std::make_shared<cancel_state>()) {}

    /**
     * @brief Obtain a cancellation token linked to this source.
     *
     * @return A cancel_token observing this source.
     */
    cancel_token token() const noexcept
    {
      return cancel_token{st_};
    }

    /**
     * @brief Request cancellation.
     *
     * Signals cancellation to all associated tokens.
     */
    void request_cancel() noexcept
    {
      if (st_)
        st_->request_cancel();
    }

    /**
     * @brief Check whether cancellation has been requested.
     *
     * @return true if cancellation was requested, false otherwise.
     */
    bool is_cancelled() const noexcept
    {
      return st_ ? st_->is_cancelled() : false;
    }

  private:
    /**
     * @brief Shared cancellation state.
     */
    std::shared_ptr<cancel_state> st_;
  };

  /**
   * @brief Standard error code for cancellation.
   *
   * @return std::error_code representing a cancelled operation.
   */
  inline std::error_code cancelled_ec() noexcept
  {
    return make_error_code(errc::canceled);
  }

} // namespace vix::async::core

#endif // VIX_ASYNC_CANCEL_HPP
