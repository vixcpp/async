cmake_minimum_required(VERSION 3.20)

include(AsyncWarnings)
include(AsyncSanitizers)

function(async_add_example name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE vix::async)
  async_apply_warnings(${name})
  async_apply_sanitizers(${name})
endfunction()

async_add_example(async_example_00_hello_task
  00_hello_task.cpp
)

async_add_example(async_example_01_signal_stop
  01_signal_stop.cpp
)

async_add_example(async_example_01_timer
  01_timer.cpp
)

async_add_example(async_example_02_thread_pool
  02_thread_pool.cpp
)

async_add_example(async_example_04_when_all_any
  when_all_any.cpp
)
