/**
 *
 *  @file async/async.hpp
 *  @author Gaspard Kirira
 *
 *  @brief Internal aggregation header for the Vix async module.
 *
 *  This file includes the core asynchronous primitives provided by Vix,
 *  including execution contexts, schedulers, tasks, timers, signals,
 *  thread pools, and networking helpers.
 *
 *  For most use cases, prefer:
 *    #include <vix/async.hpp>
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_ASYNC_ASYNC_HPP
#define VIX_ASYNC_ASYNC_HPP

#include <vix/async/version.hpp>

// core
#include <vix/async/core/cancel.hpp>
#include <vix/async/core/error.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/scheduler.hpp>
#include <vix/async/core/signal.hpp>
#include <vix/async/core/spawn.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/core/thread_pool.hpp>
#include <vix/async/core/timer.hpp>
#include <vix/async/core/when.hpp>

// net
#include <vix/async/net/asio_net_service.hpp>
#include <vix/async/net/dns.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/async/net/udp.hpp>

#endif // VIX_ASYNC_ASYNC_HPP
