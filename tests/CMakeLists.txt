cmake_minimum_required(VERSION 3.20)

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

  if (VIX_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(${test_name}
      PRIVATE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )

    target_link_options(${test_name}
      PRIVATE
        -fsanitize=address,undefined
    )
  endif()

  add_test(
    NAME ${ctest_name}
    COMMAND ${test_name}
  )
endfunction()

vix_async_add_test(async_task_smoke core/task_smoke_test.cpp async.task_smoke)
vix_async_add_test(async_cancel_smoke core/cancel_smoke_test.cpp async.cancel_smoke)
vix_async_add_test(async_scheduler_smoke core/scheduler_smoke_test.cpp async.scheduler_smoke)
vix_async_add_test(async_when_smoke core/when_smoke_test.cpp async.when_smoke)
