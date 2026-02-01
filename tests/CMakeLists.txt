cmake_minimum_required(VERSION 3.20)

add_executable(cnerium_task_smoke
  core/task_smoke_test.cpp
)

add_executable(cnerium_cancel_smoke
  core/cancel_smoke_test.cpp
)

add_executable(cnerium_scheduler_smoke
  core/scheduler_smoke_test.cpp
)

add_executable(cnerium_when_smoke
  core/when_smoke_test.cpp
)

# Link against the library
target_link_libraries(cnerium_task_smoke PRIVATE cnerium::cnerium)
target_link_libraries(cnerium_cancel_smoke PRIVATE cnerium::cnerium)
target_link_libraries(cnerium_scheduler_smoke PRIVATE cnerium::cnerium)
target_link_libraries(cnerium_when_smoke PRIVATE cnerium::cnerium)

# Keep tests strict too
cnerium_apply_warnings(cnerium_task_smoke)
cnerium_apply_warnings(cnerium_cancel_smoke)
cnerium_apply_warnings(cnerium_scheduler_smoke)
cnerium_apply_warnings(cnerium_when_smoke)

cnerium_apply_sanitizers(cnerium_task_smoke)
cnerium_apply_sanitizers(cnerium_cancel_smoke)
cnerium_apply_sanitizers(cnerium_scheduler_smoke)
cnerium_apply_sanitizers(cnerium_when_smoke)

# Register with CTest
add_test(NAME cnerium.task_smoke COMMAND cnerium_task_smoke)
add_test(NAME cnerium.cancel_smoke COMMAND cnerium_cancel_smoke)
add_test(NAME cnerium.scheduler_smoke COMMAND cnerium_scheduler_smoke)
add_test(NAME cnerium.when_smoke COMMAND cnerium_when_smoke)
