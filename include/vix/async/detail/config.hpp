/**
 *
 *  @file config.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_ASYNC_CONFIG_HPP
#define VIX_ASYNC_CONFIG_HPP

#ifndef ASYNC_VERSION_MAJOR
#define ASYNC_VERSION_MAJOR 0
#endif

#ifndef ASYNC_VERSION_MINOR
#define ASYNC_VERSION_MINOR 1
#endif

#ifndef ASYNC_VERSION_PATCH
#define ASYNC_VERSION_PATCH 0
#endif

#define ASYNC_VERSION_STRING "0.1.0"

#ifndef ASYNC_ENABLE_ASSERTS
#ifndef NDEBUG
#define ASYNC_ENABLE_ASSERTS 1
#else
#define ASYNC_ENABLE_ASSERTS 0
#endif
#endif

#if defined(_WIN32)
#define ASYNC_EXPORT __declspec(dllexport)
#define ASYNC_IMPORT __declspec(dllimport)
#else
#define ASYNC_EXPORT __attribute__((visibility("default")))
#define ASYNC_IMPORT
#endif

#endif
