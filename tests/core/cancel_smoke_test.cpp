/**
 *
 *  @file cancel_smoke_test.cpp
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
#include <cassert>
#include <iostream>

#include <vix/async/core/cancel.hpp>

using vix::async::core::cancel_source;
using vix::async::core::cancel_token;

static void test_default_token()
{
  cancel_token ct;
  assert(!ct.can_cancel());
  assert(!ct.is_cancelled());
}

static void test_cancel_flow()
{
  cancel_source src;
  auto ct = src.token();

  assert(ct.can_cancel());
  assert(!ct.is_cancelled());
  assert(!src.is_cancelled());

  src.request_cancel();

  assert(ct.is_cancelled());
  assert(src.is_cancelled());
}

int main()
{
  test_default_token();
  test_cancel_flow();

  std::cout << "async_cancel_smoke: OK\n";
  return 0;
}
