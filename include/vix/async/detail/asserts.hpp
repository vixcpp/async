/**
 *
 *  @file asserts.hpp
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
#ifndef VIX_ASYNC_ASSERTS_HPP
#define VIX_ASYNC_ASSERTS_HPP

#include <cstdlib>
#include <iostream>

#include <vix/async/detail/config.hpp>

namespace vix::async::detail
{

  [[noreturn]] inline void assert_fail(
      const char *expr,
      const char *file,
      int line,
      const char *msg = nullptr)
  {
    std::cerr << "[async][assert] failed: " << expr
              << "\n  at " << file << ":" << line;

    if (msg)
      std::cerr << "\n  message: " << msg;

    std::cerr << std::endl;
    std::abort();
  }

} // namespace vix::async::detail

// Public assertion macro
#if ASYNC_ENABLE_ASSERTS
#define ASYNC_ASSERT(expr) \
  ((expr) ? (void)0 : ::vix::async::detail::assert_fail(#expr, __FILE__, __LINE__))

#define ASYNC_ASSERT_MSG(expr, msg) \
  ((expr) ? (void)0 : ::vix::async::detail::assert_fail(#expr, __FILE__, __LINE__, msg))
#else
#define ASYNC_ASSERT(expr) ((void)0)
#define ASYNC_ASSERT_MSG(expr, msg) ((void)0)
#endif

#endif
