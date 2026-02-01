#pragma once

#include <atomic>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cnerium/core/task.hpp>
#include <cnerium/core/scheduler.hpp>

namespace cnerium::core
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

    // store_value
    template <typename T, typename U>
    inline void store_value(stored_t<T> &dst, U &&v)
    {
      using V = std::decay_t<T>;
      dst.emplace(static_cast<V>(std::forward<U>(v)));
    }

    inline void store_value(stored_t<void> &dst)
    {
      dst.emplace(std::monostate{});
    }

    // take_value
    template <typename T>
    inline std::decay_t<T> take_value(stored_t<T> &src)
    {
      return std::move(*src);
    }

    inline void take_value(stored_t<void> &)
    {
    }

  } // namespace detail

  namespace detail
  {
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
        }
        else
        {
          auto v = co_await t;
          auto &slot = std::get<I>(st->results);
          slot.emplace(std::move(v));
        }
      }
      catch (...)
      {
        // keep the first exception only
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

        // Start all runners on scheduler
        start_all(std::make_index_sequence<sizeof...(Ts)>{});
      }

      auto await_resume()
      {
        if (st->first_ex)
          std::rethrow_exception(st->first_ex);

        if constexpr (sizeof...(Ts) == 0)
        {
          return std::tuple<>{};
        }
        else
        {
          return take_results(std::make_index_sequence<sizeof...(Ts)>{});
        }
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

      template <std::size_t... Is>
      auto take_results(std::index_sequence<Is...>)
      {
        return std::tuple<decltype(result_take_one<Is, Ts>())...>{
            result_take_one<Is, Ts>()...};
      }

      template <std::size_t I, typename T>
      auto result_take_one()
      {
        if constexpr (std::is_void_v<T>)
        {
          return;
        }
        else
        {
          auto &slot = std::get<I>(st->results);
          return std::move(*slot);
        }
      }
    };
  } // namespace detail

  template <typename... Ts>
  task<std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>>
  when_all(scheduler &sched, task<Ts>... ts)
  {
    co_await sched.schedule();

    detail::when_all_awaitable<Ts...> aw{
        &sched,
        std::tuple<task<Ts>...>{std::move(ts)...}};

    auto raw = co_await aw;

    auto norm = normalize_tuple<Ts...>(std::move(raw));
    co_return norm;
  }

  namespace detail
  {
    template <typename... Ts, std::size_t... Is>
    static auto normalize_tuple_impl(std::tuple<Ts...> in, std::index_sequence<Is...>)
    {
      return std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>{
          normalize_one<Ts>(std::get<Is>(in))...};
    }

    template <typename T>
    static auto normalize_one(T &v)
    {
      return std::move(v);
    }

    static auto normalize_one(void)
    {
      return std::monostate{};
    }
  } // namespace detail

  template <typename... Ts>
  static auto normalize_tuple(std::tuple<Ts...> in)
  {
    return detail::normalize_tuple_impl<Ts...>(std::move(in), std::index_sequence_for<Ts...>{});
  }

  namespace detail
  {
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
        }
        else
        {
          auto v = co_await t;
          auto &slot = std::get<I>(st->results);
          slot.emplace(std::move(v));
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

  // when_any: returns {index, valueTuple}. For void tasks, slot is monostate.
  template <typename... Ts>
  task<std::pair<std::size_t, std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>>>
  when_any(scheduler &sched, task<Ts>... ts)
  {
    co_await sched.schedule();

    detail::when_any_awaitable<Ts...> aw{
        &sched,
        std::tuple<task<Ts>...>{std::move(ts)...}};

    auto [idx, raw] = co_await aw;

    // Convert optional<T> storage into plain Ts/monostate.
    auto out = materialize_tuple<Ts...>(std::move(raw));
    co_return {idx, std::move(out)};
  }

  namespace detail
  {
    template <typename... Ts, std::size_t... Is>
    static auto materialize_tuple_impl(std::tuple<stored_t<Ts>...> raw, std::index_sequence<Is...>)
    {
      return std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>{
          materialize_one<Ts>(std::get<Is>(raw))...};
    }

    template <typename T>
    static auto materialize_one(std::optional<T> &slot)
    {
      return std::move(*slot);
    }

    static auto materialize_one(std::monostate &)
    {
      return std::monostate{};
    }
  } // namespace detail

  template <typename... Ts>
  static auto materialize_tuple(std::tuple<detail::stored_t<Ts>...> raw)
  {
    return detail::materialize_tuple_impl<Ts...>(std::move(raw), std::index_sequence_for<Ts...>{});
  }

} // namespace cnerium::core
