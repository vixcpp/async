/**
 *
 *  @file when.hpp
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
    template <class Aw>
    struct forward_await
    {
      std::decay_t<Aw> aw;

      bool await_ready() noexcept(noexcept(aw.await_ready()))
      {
        return aw.await_ready();
      }

      auto await_suspend(std::coroutine_handle<> h) noexcept(noexcept(aw.await_suspend(h)))
      {
        return aw.await_suspend(h);
      }

      decltype(auto) await_resume() noexcept(noexcept(aw.await_resume()))
      {
        return aw.await_resume();
      }
    };

    template <class Aw>
    forward_await<Aw> as_awaitable(Aw &&aw)
    {
      return forward_await<Aw>{std::forward<Aw>(aw)};
    }

    /**
     * @brief Storage mapping for when_all/when_any results.
     *
     * For non-void T, we store std::optional<std::decay_t<T>>.
     * For void, we store std::optional<std::monostate>.
     *
     * This allows uniform "present or not" handling for each task slot.
     *
     * @tparam T Result type.
     */
    template <typename T>
    struct stored
    {
      using type = std::optional<std::decay_t<T>>;
    };

    /**
     * @brief Storage mapping for void results.
     */
    template <>
    struct stored<void>
    {
      using type = std::optional<std::monostate>;
    };

    /**
     * @brief Convenience alias for stored<T>::type.
     */
    template <typename T>
    using stored_t = typename stored<T>::type;

    /**
     * @brief Store a non-void value into a storage slot.
     *
     * @tparam T Slot logical type.
     * @param slot Destination slot.
     * @param v Value to store (moved).
     */
    template <typename T>
    inline void store_into(stored_t<T> &slot, std::decay_t<T> &&v)
    {
      slot.emplace(std::move(v));
    }

    /**
     * @brief Store completion into a void slot.
     *
     * @param slot Destination slot.
     */
    inline void store_into(stored_t<void> &slot)
    {
      slot.emplace(std::monostate{});
    }

    /**
     * @brief Move a non-void value out of a storage slot.
     *
     * @tparam T Slot logical type.
     * @param slot Source slot.
     * @return Value (moved).
     */
    template <typename T>
    inline std::decay_t<T> materialize_one(stored_t<T> &slot)
    {
      return std::move(*slot);
    }

    /**
     * @brief Materialize a void slot as std::monostate.
     *
     * @return std::monostate
     */
    inline std::monostate materialize_one(stored_t<void> &)
    {
      return std::monostate{};
    }

    /**
     * @brief Convert a tuple of stored slots into an output tuple.
     *
     * void is mapped to std::monostate in the output tuple.
     *
     * @tparam Ts Original task result types.
     * @tparam Is Index sequence.
     * @param raw Tuple of stored slots.
     * @return Output tuple with values moved out.
     */
    template <typename... Ts, std::size_t... Is>
    inline auto materialize_tuple_impl(std::tuple<stored_t<Ts>...> raw, std::index_sequence<Is...>)
    {
      using Out = std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>;
      return Out(materialize_one<Ts>(std::get<Is>(raw))...);
    }

    /**
     * @brief Convert a tuple of stored slots into an output tuple.
     *
     * @tparam Ts Original task result types.
     * @param raw Tuple of stored slots.
     * @return Output tuple with void mapped to std::monostate.
     */
    template <typename... Ts>
    inline auto materialize_tuple(std::tuple<stored_t<Ts>...> raw)
    {
      return materialize_tuple_impl<Ts...>(std::move(raw), std::index_sequence_for<Ts...>{});
    }

    /**
     * @brief Shared state for when_all.
     *
     * Tracks:
     * - remaining: number of tasks not finished yet
     * - cont: awaiting coroutine to resume when all finish
     * - first_ex: first captured exception (if any)
     * - results: stored results slots
     *
     * The scheduler pointer is used to post the continuation back to the
     * scheduler thread.
     *
     * @tparam Ts Task result types.
     */
    template <typename... Ts>
    struct when_all_state
    {
      scheduler *sched{};
      std::atomic<std::size_t> remaining{sizeof...(Ts)};
      std::coroutine_handle<> cont{};
      std::exception_ptr first_ex{};
      std::tuple<stored_t<Ts>...> results{};
    };

    /**
     * @brief Runner coroutine for one task in when_all.
     *
     * Awaits a task, stores its result, captures the first exception,
     * and when the last task completes, resumes the awaiting coroutine.
     *
     * @tparam I Index of the task in the pack.
     * @tparam T Task result type.
     * @tparam Ts Full pack types.
     * @param st Shared when_all state.
     * @param t Task to run.
     * @return task<void>
     */
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

    /**
     * @brief Awaitable implementing when_all scheduling and aggregation.
     *
     * Starts all tasks as detached runners, then resumes the awaiting coroutine
     * once all tasks complete.
     *
     * @tparam Ts Task result types.
     */
    template <typename... Ts>
    struct when_all_awaitable
    {
      scheduler *sched{};
      std::tuple<task<Ts>...> tasks;
      std::shared_ptr<when_all_state<Ts...>> st{};

      /**
       * @brief Always suspend to start tasks concurrently.
       */
      bool await_ready() const noexcept { return false; }

      /**
       * @brief Start all runners and suspend the awaiting coroutine.
       *
       * @param h Awaiting coroutine handle.
       */
      void await_suspend(std::coroutine_handle<> h)
      {
        st = std::make_shared<when_all_state<Ts...>>();
        st->sched = sched;
        st->cont = h;
        start_all(std::make_index_sequence<sizeof...(Ts)>{});
      }

      /**
       * @brief Resume and return aggregated results (or rethrow first exception).
       *
       * @return Tuple of results with void mapped to std::monostate.
       * @throws First exception captured by any task.
       */
      auto await_resume()
      {
        if (st->first_ex)
          std::rethrow_exception(st->first_ex);
        return materialize_tuple<Ts...>(std::move(st->results));
      }

    private:
      /**
       * @brief Start all tasks using detached runner coroutines.
       */
      template <std::size_t... Is>
      void start_all(std::index_sequence<Is...>)
      {
        (start_one<Is, Ts>(std::get<Is>(tasks)), ...);
      }

      /**
       * @brief Start one runner for a specific task slot.
       *
       * @tparam I Index.
       * @tparam T Task result type.
       * @param t Task slot reference.
       */
      template <std::size_t I, typename T>
      void start_one(task<T> &t)
      {
        auto runner = when_all_runner<I, T, Ts...>(st, std::move(t));
        std::move(runner).start(*sched);
      }
    };

    /**
     * @brief Shared state for when_any.
     *
     * Tracks:
     * - done: first completion flag
     * - cont: awaiting coroutine to resume on first completion
     * - ex: first captured exception (if any)
     * - index: index of the first task that completed
     * - results: stored results slots (only guaranteed to have the winner populated)
     *
     * @tparam Ts Task result types.
     */
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

    /**
     * @brief Runner coroutine for one task in when_any.
     *
     * Awaits a task, stores its result (or captures exception),
     * and if this runner wins the race, resumes the awaiting coroutine.
     *
     * @tparam I Index of the task in the pack.
     * @tparam T Task result type.
     * @tparam Ts Full pack types.
     * @param st Shared when_any state.
     * @param t Task to run.
     * @return task<void>
     */
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

    /**
     * @brief Awaitable implementing when_any scheduling and first-completion semantics.
     *
     * Starts all tasks as detached runners, then resumes the awaiting coroutine
     * once the first task completes (success or exception).
     *
     * @tparam Ts Task result types.
     */
    template <typename... Ts>
    struct when_any_awaitable
    {
      scheduler *sched{};
      std::tuple<task<Ts>...> tasks;
      std::shared_ptr<when_any_state<Ts...>> st{};

      /**
       * @brief Always suspend to start tasks concurrently.
       */
      bool await_ready() const noexcept { return false; }

      /**
       * @brief Start all runners and suspend the awaiting coroutine.
       *
       * @param h Awaiting coroutine handle.
       */
      void await_suspend(std::coroutine_handle<> h)
      {
        st = std::make_shared<when_any_state<Ts...>>();
        st->sched = sched;
        st->cont = h;
        start_all(std::make_index_sequence<sizeof...(Ts)>{});
      }

      /**
       * @brief Resume and return the winning index and raw stored slots.
       *
       * The returned tuple contains stored_t<T> slots. Use materialize_tuple()
       * to map void to std::monostate and move values out.
       *
       * @return Pair {index, tuple<stored slots>}.
       * @throws First exception captured (if winner failed and ex is set).
       */
      std::pair<std::size_t, std::tuple<stored_t<Ts>...>> await_resume()
      {
        if (st->ex)
          std::rethrow_exception(st->ex);

        return {st->index, std::move(st->results)};
      }

    private:
      /**
       * @brief Start all tasks using detached runner coroutines.
       */
      template <std::size_t... Is>
      void start_all(std::index_sequence<Is...>)
      {
        (start_one<Is, Ts>(std::get<Is>(tasks)), ...);
      }

      /**
       * @brief Start one runner for a specific task slot.
       *
       * @tparam I Index.
       * @tparam T Task result type.
       * @param t Task slot reference.
       */
      template <std::size_t I, typename T>
      void start_one(task<T> &t)
      {
        auto runner = when_any_runner<I, T, Ts...>(st, std::move(t));
        std::move(runner).start(*sched);
      }
    };

  } // namespace detail

  /**
   * @brief Await completion of all tasks.
   *
   * Runs all provided tasks concurrently and returns a tuple of results in the
   * same order as the input arguments. For task<void>, the corresponding output
   * element is std::monostate.
   *
   * If any task throws, the first captured exception is rethrown when resuming.
   *
   * @tparam Ts Task result types.
   * @param sched Scheduler used to start and resume continuations.
   * @param ts Tasks to run.
   * @return task<std::tuple<...>> aggregated results (void mapped to monostate).
   */
  template <typename... Ts>
  task<std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>>
  when_all(scheduler &sched, task<Ts>... ts)
  {
    co_await sched.schedule();

    detail::when_all_awaitable<Ts...> aw{
        &sched,
        std::tuple<task<Ts>...>{std::move(ts)...}};

    auto out = co_await detail::as_awaitable(std::move(aw));
    co_return out;
  }

  /**
   * @brief Await completion of any task (first winner).
   *
   * Runs all provided tasks concurrently and completes when the first task
   * finishes (success or exception). Returns:
   * - index: the winning task index
   * - tuple: aggregated results with void mapped to std::monostate
   *
   * If the winning task throws (or the first captured exception is recorded),
   * the exception is rethrown when resuming.
   *
   * @tparam Ts Task result types.
   * @param sched Scheduler used to start and resume continuations.
   * @param ts Tasks to run.
   * @return task<pair<index, tuple<...>>> with void mapped to monostate.
   */
  template <typename... Ts>
  task<std::pair<std::size_t, std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>>>
  when_any(scheduler &sched, task<Ts>... ts)
  {
    co_await sched.schedule();

    detail::when_any_awaitable<Ts...> aw{
        &sched,
        std::tuple<task<Ts>...>{std::move(ts)...}};

    auto [idx, raw] = co_await detail::as_awaitable(std::move(aw));

    using OutTuple = std::tuple<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>;
    using Ret = std::pair<std::size_t, OutTuple>;

    Ret r;
    r.first = idx;
    r.second = detail::materialize_tuple<Ts...>(std::move(raw));
    co_return r;
  }

} // namespace vix::async::core

#endif // VIX_ASYNC_WHEN_HPP
