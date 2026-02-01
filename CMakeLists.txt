# ====================================================================
# Vix.cpp - Async module
# - Standalone: find_package(async) works (asyncTargets + asyncConfig.cmake)
# - Umbrella  : contributes to VixTargets only (umbrella owns packaging)
# ====================================================================

# Standalone vs Umbrella
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(vix_async VERSION 0.1.0 LANGUAGES CXX)

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

include(GNUInstallDirs)

# Options + helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AsyncOptions)
include(AsyncWarnings)
include(AsyncSanitizers)

# ----------------------------------------------------
# Asio policy (same spirit as p2p)
# - Umbrella: must provide vix::thirdparty_asio
# - Standalone: allow module-local vendored Asio (third_party/asio) OR system headers
# ----------------------------------------------------
function(vix_async_link_asio onto link_scope)
  # Umbrella mode
  if (DEFINED VIX_UMBRELLA_BUILD AND VIX_UMBRELLA_BUILD)
    if (TARGET vix::thirdparty_asio)
      target_link_libraries(${onto} ${link_scope} vix::thirdparty_asio)
      target_compile_definitions(${onto} ${link_scope} VIX_ASYNC_WITH_ASIO=1)
      return()
    endif()
    message(FATAL_ERROR "[async] Umbrella build: vix::thirdparty_asio must be provided by umbrella.")
  endif()

  # Standalone mode: try module-local first
  set(_asio_local "${CMAKE_CURRENT_LIST_DIR}/third_party/asio/include")
  if (EXISTS "${_asio_local}/asio.hpp")
    target_include_directories(${onto} SYSTEM ${link_scope} "${_asio_local}")
    target_compile_definitions(${onto} ${link_scope} VIX_ASYNC_WITH_ASIO=1)
    message(STATUS "[async] Asio: using module-local Asio at ${_asio_local}")
    return()
  endif()

  # System Asio (headers-only)
  find_path(ASIO_INCLUDE_DIR
    NAMES asio.hpp
    HINTS
      /usr/include
      /usr/local/include
      $ENV{ASIO_ROOT}/include
      $ENV{ASIO_ROOT}
  )

  if (NOT ASIO_INCLUDE_DIR)
    message(FATAL_ERROR
      "[async] Asio not found.\n"
      "Fix:\n"
      "  - vendor Asio in modules/async/third_party/asio/include\n"
      "  - or install standalone Asio headers\n"
      "  - or build inside Vix umbrella (provides vix::thirdparty_asio)\n"
    )
  endif()

  target_include_directories(${onto} SYSTEM ${link_scope} "${ASIO_INCLUDE_DIR}")
  target_compile_definitions(${onto} ${link_scope} VIX_ASYNC_WITH_ASIO=1)
  message(STATUS "[async] Asio: using system Asio at ${ASIO_INCLUDE_DIR}")
endfunction()

# Common Asio requirements
function(vix_async_apply_asio_common onto link_scope)
  target_compile_definitions(${onto} ${link_scope} ASIO_STANDALONE=1)
  if (UNIX AND NOT APPLE)
    target_link_libraries(${onto} ${link_scope} pthread)
  endif()
endfunction()

# ----------------------------------------------------
# Library target
# ----------------------------------------------------
add_library(vix_async)
add_library(vix::async ALIAS vix_async)

target_compile_features(vix_async PUBLIC cxx_std_20)

# Public headers live under include/vix/async/...
target_include_directories(vix_async
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Sources + headers
file(GLOB_RECURSE VIX_ASYNC_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE VIX_ASYNC_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/vix/async/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/vix/async/*.h"
)

target_sources(vix_async
  PRIVATE
    ${VIX_ASYNC_SOURCES}
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
      FILES ${VIX_ASYNC_HEADERS}
)

# Asio link (policy)
vix_async_link_asio(vix_async PUBLIC)
vix_async_apply_asio_common(vix_async PUBLIC)

# Warnings / sanitizers (module local)
async_apply_warnings(vix_async)
async_apply_sanitizers(vix_async)

set_target_properties(vix_async PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  OUTPUT_NAME vix_async
  EXPORT_NAME async
)

# ----------------------------------------------------
# Tests / Examples
# ----------------------------------------------------
include(CTest)

if (ASYNC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if (ASYNC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ----------------------------------------------------
# Install + export
# - Standalone: installs asyncTargets + asyncConfig.cmake
# - Umbrella: contributes target to VixTargets only (umbrella owns packaging)
# ----------------------------------------------------
set(_ASYNC_EXPORT_SET "asyncTargets")
if (DEFINED VIX_UMBRELLA_BUILD AND VIX_UMBRELLA_BUILD)
  set(_ASYNC_EXPORT_SET "VixTargets")
endif()

install(
  TARGETS vix_async
  EXPORT ${_ASYNC_EXPORT_SET}
  FILE_SET HEADERS
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Standalone package exports (only when not built via umbrella)
if (NOT (DEFINED VIX_UMBRELLA_BUILD AND VIX_UMBRELLA_BUILD))
  install(
    EXPORT asyncTargets
    NAMESPACE vix::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/async
  )

  include(CMakePackageConfigHelpers)

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/asyncConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/asyncConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/asyncConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/async"
  )

  install(
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/asyncConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/asyncConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/async
  )
endif()
