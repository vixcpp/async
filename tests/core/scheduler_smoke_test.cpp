#include <cassert>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>

#include <cnerium/core/scheduler.hpp>

using cnerium::core::scheduler;

int main()
{
  scheduler sched;

  std::atomic<int> counter{0};

  // Post a few jobs before run()
  sched.post([&]()
             { counter.fetch_add(1); });
  sched.post([&]()
             { counter.fetch_add(1); });

  // Run the event loop in another thread
  std::thread loop(
      [&]()
      { sched.run(); });

  // Thread-safe post from main thread
  for (int i = 0; i < 10; ++i)
  {
    sched.post([&]()
               { counter.fetch_add(1); });
  }

  // Give it a tiny moment to drain
  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  // Ask it to stop, then join
  sched.stop();
  loop.join();

  // We expect at least 12 increments (2 before + 10 after).
  // It can never be less if run() started correctly and we waited a bit.
  assert(counter.load() >= 12);

  std::cout << "cnerium_scheduler_smoke: OK\n";
  return 0;
}
