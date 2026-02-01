cmake_minimum_required(VERSION 3.20)

include(CneriumWarnings)
include(CneriumSanitizers)

function(cnerium_add_example name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE cnerium::cnerium)
  cnerium_apply_warnings(${name})
  cnerium_apply_sanitizers(${name})
endfunction()

cnerium_add_example(cnerium_example_00_hello_task
  00_hello_task.cpp
)

cnerium_add_example(cnerium_example_01_signal_stop
  01_signal_stop.cpp
)

cnerium_add_example(cnerium_example_01_timer
  01_timer.cpp
)

cnerium_add_example(cnerium_example_02_thread_pool
  02_thread_pool.cpp
)

cnerium_add_example(cnerium_example_04_when_all_any
  when_all_any.cpp
)
