#pragma once

#include <cstdlib>
#include <iostream>

#include <cnerium/detail/config.hpp>

namespace cnerium::detail
{

  [[noreturn]] inline void assert_fail(
      const char *expr,
      const char *file,
      int line,
      const char *msg = nullptr)
  {
    std::cerr << "[cnerium][assert] failed: " << expr
              << "\n  at " << file << ":" << line;

    if (msg)
      std::cerr << "\n  message: " << msg;

    std::cerr << std::endl;
    std::abort();
  }

} // namespace cnerium::detail

// Public assertion macro
#if CNERIUM_ENABLE_ASSERTS
#define CNERIUM_ASSERT(expr) \
  ((expr) ? (void)0 : ::cnerium::detail::assert_fail(#expr, __FILE__, __LINE__))

#define CNERIUM_ASSERT_MSG(expr, msg) \
  ((expr) ? (void)0 : ::cnerium::detail::assert_fail(#expr, __FILE__, __LINE__, msg))
#else
#define CNERIUM_ASSERT(expr) ((void)0)
#define CNERIUM_ASSERT_MSG(expr, msg) ((void)0)
#endif
