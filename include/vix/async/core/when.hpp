/**
 *
 *  @file when.hpp
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
#ifndef VIX_ASYNC_WHEN_HPP
#define VIX_ASYNC_WHEN_HPP

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <vix/async/core/task.hpp>
#include <vix/async/core/scheduler.hpp>

namespace vix::async::core
{
  namespace detail
  {
    template <typename T>
    struct stored
    {
      using type = std::optional<std::decay_t<T>>;
    };

    template <>
    struct stored<void>
    {
      using type = std::optional<std::monostate>;
    };

    template <typename T>
    using stored_t = typename stored<T>::type;

    template <typename T>
    inline void store_into(stored_t<T> &slot, std::decay_t<T> &&v)
    {
      slot.emplace(std::move(v));
    }

    inline void store_into(stored_t<void> &slot)
    {
      slot.emplace(std::monostate{});
    }

    template <typename T>
    inline std::decay_t<T> materialize_one(stored_t<T> &slot)
    {
      return std::move(*slot);
    }

    inline std::monostate materialize_one(stored_t<void> &)
    {
      return std::monostate{};
    }

    template <typename... Ts, std::size_t... Is>
    inline auto materialize_tuple_impl(std::tuple<stored_t<Ts>...> raw, std::index_sequence<Is...>)
    {
      using Out = std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>;
      return Out(materialize_one<Ts>(std::get<Is>(raw))...);
    }

    template <typename... Ts>
    inline auto materialize_tuple(std::tuple<stored_t<Ts>...> raw)
    {
      return materialize_tuple_impl<Ts...>(std::move(raw), std::index_sequence_for<Ts...>{});
    }

    template <typename... Ts>
    struct when_all_state
    {
      scheduler *sched{};
      std::atomic<std::size_t> remaining{sizeof...(Ts)};
      std::coroutine_handle<> cont{};
      std::exception_ptr first_ex{};
      std::tuple<stored_t<Ts>...> results{};
    };

    template <std::size_t I, typename T, typename... Ts>
    task<void> when_all_runner(std::shared_ptr<when_all_state<Ts...>> st, task<T> t)
    {
      try
      {
        if constexpr (std::is_void_v<T>)
        {
          co_await t;
          store_into<T>(std::get<I>(st->results));
        }
        else
        {
          auto v = co_await t;
          store_into<T>(std::get<I>(st->results), std::move(v));
        }
      }
      catch (...)
      {
        if (!st->first_ex)
          st->first_ex = std::current_exception();
      }

      if (st->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
      {
        if (st->sched)
          st->sched->post(st->cont);
        else if (st->cont)
          st->cont.resume();
      }

      co_return;
    }

    template <typename... Ts>
    struct when_all_awaitable
    {
      scheduler *sched{};
      std::tuple<task<Ts>...> tasks;
      std::shared_ptr<when_all_state<Ts...>> st{};

      bool await_ready() const noexcept { return false; }

      void await_suspend(std::coroutine_handle<> h)
      {
        st = std::make_shared<when_all_state<Ts...>>();
        st->sched = sched;
        st->cont = h;
        start_all(std::make_index_sequence<sizeof...(Ts)>{});
      }

      auto await_resume()
      {
        if (st->first_ex)
          std::rethrow_exception(st->first_ex);
        return materialize_tuple<Ts...>(std::move(st->results));
      }

    private:
      template <std::size_t... Is>
      void start_all(std::index_sequence<Is...>)
      {
        (start_one<Is, Ts>(std::get<Is>(tasks)), ...);
      }

      template <std::size_t I, typename T>
      void start_one(task<T> &t)
      {
        auto runner = when_all_runner<I, T, Ts...>(st, std::move(t));
        std::move(runner).start(*sched);
      }
    };

    template <typename... Ts>
    struct when_any_state
    {
      scheduler *sched{};
      std::atomic<bool> done{false};
      std::coroutine_handle<> cont{};
      std::exception_ptr ex{};
      std::size_t index{static_cast<std::size_t>(-1)};
      std::tuple<stored_t<Ts>...> results{};
    };

    template <std::size_t I, typename T, typename... Ts>
    task<void> when_any_runner(std::shared_ptr<when_any_state<Ts...>> st, task<T> t)
    {
      try
      {
        if constexpr (std::is_void_v<T>)
        {
          co_await t;
          store_into<T>(std::get<I>(st->results));
        }
        else
        {
          auto v = co_await t;
          store_into<T>(std::get<I>(st->results), std::move(v));
        }
      }
      catch (...)
      {
        if (!st->ex)
          st->ex = std::current_exception();
      }

      bool expected = false;
      if (st->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      {
        st->index = I;
        if (st->sched)
          st->sched->post(st->cont);
        else if (st->cont)
          st->cont.resume();
      }

      co_return;
    }

    template <typename... Ts>
    struct when_any_awaitable
    {
      scheduler *sched{};
      std::tuple<task<Ts>...> tasks;
      std::shared_ptr<when_any_state<Ts...>> st{};

      bool await_ready() const noexcept { return false; }

      void await_suspend(std::coroutine_handle<> h)
      {
        st = std::make_shared<when_any_state<Ts...>>();
        st->sched = sched;
        st->cont = h;
        start_all(std::make_index_sequence<sizeof...(Ts)>{});
      }

      std::pair<std::size_t, std::tuple<stored_t<Ts>...>> await_resume()
      {
        if (st->ex)
          std::rethrow_exception(st->ex);

        return {st->index, std::move(st->results)};
      }

    private:
      template <std::size_t... Is>
      void start_all(std::index_sequence<Is...>)
      {
        (start_one<Is, Ts>(std::get<Is>(tasks)), ...);
      }

      template <std::size_t I, typename T>
      void start_one(task<T> &t)
      {
        auto runner = when_any_runner<I, T, Ts...>(st, std::move(t));
        std::move(runner).start(*sched);
      }
    };

  } // namespace detail

  // when_all: returns tuple<Ts...> with void mapped to monostate
  template <typename... Ts>
  task<std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>>
  when_all(scheduler &sched, task<Ts>... ts)
  {
    co_await sched.schedule();

    detail::when_all_awaitable<Ts...> aw{
        &sched,
        std::tuple<task<Ts>...>{std::move(ts)...}};

    auto out = co_await aw;
    co_return out;
  }

  // when_any: returns {index, tuple<Ts...>} with void mapped to monostate
  template <typename... Ts>
  task<std::pair<std::size_t, std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>>>
  when_any(scheduler &sched, task<Ts>... ts)
  {
    co_await sched.schedule();

    detail::when_any_awaitable<Ts...> aw{
        &sched,
        std::tuple<task<Ts>...>{std::move(ts)...}};

    auto [idx, raw] = co_await aw;

    using OutTuple = std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>;
    using Ret = std::pair<std::size_t, OutTuple>;

    Ret r;
    r.first = idx;
    r.second = detail::materialize_tuple<Ts...>(std::move(raw));
    co_return r;
  }

} // namespace vix::async::core

#endif
