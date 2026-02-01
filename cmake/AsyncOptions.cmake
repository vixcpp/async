include_guard(GLOBAL)

option(ASYNC_BUILD_TESTS "Build Async tests" ON)
option(ASYNC_BUILD_EXAMPLES "Build Async examples" OFF)

option(ASYNC_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

option(ASYNC_ENABLE_SANITIZERS "Enable AddressSanitizer + UBSan (where supported)" OFF)
option(ASYNC_ENABLE_TSAN "Enable ThreadSanitizer (where supported)" OFF)

option(ASYNC_USE_MOLD "Use mold linker when available (Linux only)" OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

if(ASYNC_USE_MOLD AND UNIX AND NOT APPLE)
  find_program(ASYNC_MOLD_EXE mold)
  if(ASYNC_MOLD_EXE)
    message(STATUS "Async: using mold linker: ${ASYNC_MOLD_EXE}")
    add_link_options(-fuse-ld=mold)
  else()
    message(STATUS "Async: mold requested but not found")
  endif()
endif()
