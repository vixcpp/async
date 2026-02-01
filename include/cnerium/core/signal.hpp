#pragma once

#include <csignal>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <cnerium/core/task.hpp>
#include <cnerium/core/cancel.hpp>
#include <cnerium/core/error.hpp>

namespace cnerium::core
{
  class io_context;

  class signal_set
  {
  public:
    explicit signal_set(io_context &ctx);
    ~signal_set();

    signal_set(const signal_set &) = delete;
    signal_set &operator=(const signal_set &) = delete;

    void add(int sig);
    void remove(int sig);
    task<int> async_wait(cancel_token ct = {});
    void on_signal(std::function<void(int)> fn);
    void stop() noexcept;

  private:
    void start_if_needed();
    void worker_loop();

    void ctx_post(std::function<void()> fn);

  private:
    io_context &ctx_;
    std::mutex m_;
    std::vector<int> signals_;
    std::function<void(int)> on_signal_{};
    std::queue<int> pending_;
    bool started_{false};
    bool stop_{false};
    std::thread worker_;
    std::coroutine_handle<> waiter_{};
    bool waiter_active_{false};
  };

} // namespace cnerium::core
