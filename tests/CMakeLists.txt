cmake_minimum_required(VERSION 3.20)

add_executable(async_task_smoke
  core/task_smoke_test.cpp
)

add_executable(async_cancel_smoke
  core/cancel_smoke_test.cpp
)

add_executable(async_scheduler_smoke
  core/scheduler_smoke_test.cpp
)

add_executable(async_when_smoke
  core/when_smoke_test.cpp
)

# Link against the library
target_link_libraries(async_task_smoke PRIVATE vix::async)
target_link_libraries(async_cancel_smoke PRIVATE vix::async)
target_link_libraries(async_scheduler_smoke PRIVATE vix::async)
target_link_libraries(async_when_smoke PRIVATE vix::async)

# Keep tests strict too
async_apply_warnings(async_task_smoke)
async_apply_warnings(async_cancel_smoke)
async_apply_warnings(async_scheduler_smoke)
async_apply_warnings(async_when_smoke)

async_apply_sanitizers(async_task_smoke)
async_apply_sanitizers(async_cancel_smoke)
async_apply_sanitizers(async_scheduler_smoke)
async_apply_sanitizers(async_when_smoke)

# Register with CTest
add_test(NAME async.task_smoke       COMMAND async_task_smoke)
add_test(NAME async.cancel_smoke     COMMAND async_cancel_smoke)
add_test(NAME async.scheduler_smoke  COMMAND async_scheduler_smoke)
add_test(NAME async.when_smoke       COMMAND async_when_smoke)
