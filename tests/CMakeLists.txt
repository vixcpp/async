cmake_minimum_required(VERSION 3.20)

include(AsyncSanitizers)

function(vix_async_add_test test_name test_source ctest_name)
  add_executable(${test_name}
    ${test_source}
  )

  target_compile_features(${test_name}
    PRIVATE
      cxx_std_20
  )

  target_link_libraries(${test_name}
    PRIVATE
      vix::async
  )

  async_apply_warnings(${test_name})
  async_apply_sanitizers(${test_name})

  add_test(
    NAME ${ctest_name}
    COMMAND ${test_name}
  )
endfunction()

vix_async_add_test(async_task_smoke core/task_smoke_test.cpp async.task_smoke)
vix_async_add_test(async_cancel_smoke core/cancel_smoke_test.cpp async.cancel_smoke)
vix_async_add_test(async_scheduler_smoke core/scheduler_smoke_test.cpp async.scheduler_smoke)
vix_async_add_test(async_when_smoke core/when_smoke_test.cpp async.when_smoke)
vix_async_add_test(async_timer_regression core/timer_regression_test.cpp async.timer_regression)
vix_async_add_test(async_thread_pool_regression core/thread_pool_regression_test.cpp async.thread_pool_regression)
vix_async_add_test(async_signal_regression core/signal_regression_test.cpp async.signal_regression)
vix_async_add_test(async_io_context_regression core/io_context_regression_test.cpp async.io_context_regression)
vix_async_add_test(async_net_cancellation_regression net/net_cancellation_regression_test.cpp async.net_cancellation_regression)
vix_async_add_test(async_network_smoke net/network_smoke_test.cpp async.network_smoke)
