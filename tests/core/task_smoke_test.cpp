/**
 *
 *  @file task_smoke_test.cpp
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
#include <stdexcept>
#include <string>
#include <utility>

#include <vix/async/core/task.hpp>

using vix::async::core::task;

static task<int> compute_value()
{
  co_return 42;
}

static task<int> add_one(int x)
{
  co_return x + 1;
}

static task<int> chain()
{
  int v = co_await compute_value();
  int r = co_await add_one(v);
  co_return r; // 43
}

static task<void> throws_task()
{
  throw std::runtime_error("boom");
  co_return;
}

static task<void> chain_void()
{
  assert((co_await chain()) == 43);
  co_return;
}

template <typename T>
static T sync_await(task<T> t)
{
  auto h = t.handle();
  assert(h);

  while (!h.done())
    h.resume();

  auto &p = h.promise();

  if (p.exception)
    std::rethrow_exception(p.exception);

  assert(p.value.has_value());
  return std::move(*p.value);
}

static void sync_await(task<void> t)
{
  auto h = t.handle();
  assert(h);

  while (!h.done())
    h.resume();

  auto &p = h.promise();

  if (p.exception)
    std::rethrow_exception(p.exception);
}

int main()
{
  // Basic chain
  {
    assert(sync_await(chain()) == 43);
  }

  // Void chain
  {
    sync_await(chain_void());
  }

  // Exception propagation
  {
    bool caught = false;

    try
    {
      sync_await(throws_task());
    }
    catch (const std::runtime_error &e)
    {
      caught = true;
      assert(std::string(e.what()).find("boom") != std::string::npos);
    }

    if (!caught)
      return 2;
  }

  std::cout << "async_task_smoke: OK\n";
  return 0;
}
