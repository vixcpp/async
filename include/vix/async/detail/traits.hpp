/**
 *
 *  @file traits.hpp
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
#ifndef ASYNC_TRAITS_HPP
#define ASYNC_TRAITS_HPP

#include <type_traits>
#include <utility>

namespace vix::async::detail
{
  template <typename T>
  using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

  template <typename T>
  inline constexpr bool is_void_v = std::is_void_v<T>;

  template <typename T>
  inline constexpr bool is_nothrow_move_v = std::is_nothrow_move_constructible_v<T>;

  template <typename F, typename... Args>
  using is_invocable = std::is_invocable<F, Args...>;

  template <typename F, typename... Args>
  inline constexpr bool is_invocable_v = is_invocable<F, Args...>::value;

} // namespace vix::async::detail

#endif
