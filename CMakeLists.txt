cmake_minimum_required(VERSION 3.20)

project(cnerium
  VERSION 0.1.0
  DESCRIPTION "Cnerium: async C++ core (scheduler, tasks, timers, cancellation)"
  LANGUAGES CXX
)

# Options + helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CneriumOptions)
include(CneriumWarnings)
include(CneriumSanitizers)

# Dependency options
option(CNERIUM_USE_BUNDLED_ASIO "Use vendored Asio from third_party/asio" ON)
set(CNERIUM_ASIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio"
  CACHE PATH "Path to vendored Asio repository root"
)

# Asio setup
# Creates target: asio::asio
add_library(asio::asio INTERFACE IMPORTED GLOBAL)

if(CNERIUM_USE_BUNDLED_ASIO)
  # Expected structure: <root>/asio/include
  set(_asio_include "${CNERIUM_ASIO_ROOT}/asio/include")
  if(NOT EXISTS "${_asio_include}/asio.hpp")
    message(FATAL_ERROR
      "Bundled Asio not found.\n"
      "Expected: ${_asio_include}/asio.hpp\n"
      "Fix: git submodule update --init --recursive\n"
      "Or set -DCNERIUM_USE_BUNDLED_ASIO=OFF to use system Asio."
    )
  endif()

  set_target_properties(asio::asio PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_asio_include}"
  )
else()
  # System Asio (headers-only). We do not use find_package because Asio often has no config package.
  find_path(ASIO_INCLUDE_DIR
    NAMES asio.hpp
    HINTS
      /usr/include
      /usr/local/include
      $ENV{ASIO_ROOT}/include
      $ENV{ASIO_ROOT}
  )

  if(NOT ASIO_INCLUDE_DIR)
    message(FATAL_ERROR
      "System Asio not found.\n"
      "Install standalone Asio headers or enable bundled Asio:\n"
      "  -DCNERIUM_USE_BUNDLED_ASIO=ON\n"
      "Or provide -DASIO_INCLUDE_DIR=/path/to/asio/include"
    )
  endif()

  set_target_properties(asio::asio PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ASIO_INCLUDE_DIR}"
  )
endif()

# Common Asio requirements
target_compile_definitions(asio::asio INTERFACE ASIO_STANDALONE)

if(UNIX AND NOT APPLE)
  target_link_libraries(asio::asio INTERFACE pthread)
endif()

# Library target
add_library(cnerium)
add_library(cnerium::cnerium ALIAS cnerium)

target_compile_features(cnerium PUBLIC cxx_std_20)

target_include_directories(cnerium
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)

target_sources(cnerium
  PRIVATE
    src/core/scheduler.cpp
    src/core/thread_pool.cpp
    src/core/timer.cpp
    src/core/signal.cpp
    src/net/asio_net_service.cpp
    src/net/asio_tcp.cpp
    src/net/asio_udp.cpp
    src/net/asio_dns.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
      include/cnerium/version.hpp
      include/cnerium/core/io_context.hpp
      include/cnerium/core/scheduler.hpp
      include/cnerium/core/task.hpp
      include/cnerium/core/cancel.hpp
      include/cnerium/core/timer.hpp
      include/cnerium/core/signal.hpp
      include/cnerium/core/thread_pool.hpp
      include/cnerium/core/error.hpp
      include/cnerium/core/spawn.hpp
      include/cnerium/core/when.hpp
)

# Link Asio target
target_link_libraries(cnerium PUBLIC asio::asio)

# Warnings / sanitizers
cnerium_apply_warnings(cnerium)
cnerium_apply_sanitizers(cnerium)

# Build settings
set_target_properties(cnerium PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

# Tests / Examples
include(CTest)

if(CNERIUM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CNERIUM_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Install + export (for find_package)
include(GNUInstallDirs)

install(
  TARGETS cnerium
  EXPORT cneriumTargets
  FILE_SET HEADERS
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
  EXPORT cneriumTargets
  NAMESPACE cnerium::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cnerium
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cneriumConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cneriumConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cneriumConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cnerium"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cneriumConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cneriumConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cnerium"
)
