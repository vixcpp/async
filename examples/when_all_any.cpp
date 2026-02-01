#include <iostream>
#include <thread>
#include <tuple>

#include <vix/async/core/task.hpp>
#include <vix/async/core/when.hpp>
#include <vix/async/core/scheduler.hpp>

using vix::async::core::scheduler;
using vix::async::core::task;
using vix::async::core::when_all;
using vix::async::core::when_any;

task<int> a()
{
  co_return 1;
}

task<int> b()
{
  co_return 2;
}

task<void> demo(scheduler &sched)
{
  co_await sched.schedule();

  auto tup = co_await when_all(sched, a(), b());
  std::cout << "when_all: "
            << std::get<0>(tup) << ", "
            << std::get<1>(tup) << "\n";

  auto [idx, vals] = co_await when_any(sched, a(), b());

  std::cout << "when_any: index=" << idx << " value=";
  if (idx == 0)
    std::cout << std::get<0>(vals);
  else
    std::cout << std::get<1>(vals);
  std::cout << "\n";

  sched.stop();
  co_return;
}

int main()
{
  scheduler sched;

  std::thread worker([&]()
                     { sched.run(); });

  std::move(demo(sched)).start(sched);

  worker.join();
  return 0;
}
