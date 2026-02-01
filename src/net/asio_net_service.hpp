#pragma once
#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace cnerium::core
{
  class io_context;
}

namespace cnerium::net::detail
{
  class asio_net_service
  {
  public:
    using guard_t = asio::executor_work_guard<asio::io_context::executor_type>;

    explicit asio_net_service(cnerium::core::io_context &ctx);
    ~asio_net_service();

    asio_net_service(const asio_net_service &) = delete;
    asio_net_service &operator=(const asio_net_service &) = delete;

    asio::io_context &asio_ctx() noexcept { return ioc_; }
    void stop() noexcept;

  private:
    cnerium::core::io_context &ctx_;
    asio::io_context ioc_;
    std::unique_ptr<guard_t> guard_;
    std::thread net_thread_;
    std::atomic_bool stopped_{false};
  };
}
