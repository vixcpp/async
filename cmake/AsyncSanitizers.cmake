include_guard(GLOBAL)

function(async_apply_sanitizers target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "async_apply_sanitizers: target '${target_name}' not found")
  endif()

  if(MSVC)
    return()
  endif()

  if(ASYNC_ENABLE_TSAN AND ASYNC_ENABLE_SANITIZERS)
    message(FATAL_ERROR "Enable either TSAN or ASAN/UBSAN, not both")
  endif()

  if(ASYNC_ENABLE_TSAN)
    target_compile_options(${target_name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target_name} PRIVATE -fsanitize=thread)
  endif()

  if(ASYNC_ENABLE_SANITIZERS)
    target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
