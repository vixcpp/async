# async

**async** is a **low-level C++20 asynchronous runtime core**, extracted and refined from real-world runtime needs (Vix.cpp, Softadastra).

It provides a **deterministic, explicit, coroutine-first async foundation** for building:

- asynchronous libraries
- event-driven servers
- runtimes
- networking stacks

`async` is **not a framework**.
It is a **runtime primitive**.

---

## Why async exists

C++ has powerful building blocks (threads, atomics, coroutines, Asio), but **no coherent async execution model**:

- `std::async` is underspecified
- coroutines have no runtime
- executors are fragmented
- user code easily becomes racy
- callback-based APIs hide control flow

**async** solves this by providing:

- a **single explicit event loop**
- **coroutine-based async APIs**
- **deterministic execution**
- **clear separation of concerns**

---

## Design goals

- **Async-first**: no blocking user APIs
- **Single event loop** for user code (Node/libuv-style)
- **Explicit scheduling**
- **Deterministic coroutine resumption**
- **Zero hidden threads**
- **No magic, no macros**
- **Composable cancellation**

---

## Execution model

```
[ event loop thread ]
        ↑
   coroutine resume
        ↓
[ timers | net | cpu pool ]
        ↑
   completion events
```

Rules:

- All user coroutine code runs on **one event loop thread**
- CPU-heavy work runs on a **dedicated thread pool**
- I/O runs on a **network backend thread**
- All async completions are **rescheduled onto the event loop**

This guarantees:

- no data races in user code
- predictable execution
- simple mental model

---

## Core components

| Component      | Responsibility |
|---------------|----------------|
| `io_context`  | Owns the event loop and runtime services |
| `scheduler`   | Deterministic task scheduling |
| `task<T>`     | Coroutine return type |
| `thread_pool` | CPU-bound work offloading |
| `timer`       | Non-blocking timers |
| `signal_set`  | OS signal handling |
| `cancel_token`| Cooperative cancellation |
| `net::*`      | Async networking interfaces |

---

## Minimal example

```cpp
#include <async/core/io_context.hpp>
#include <async/core/task.hpp>
#include <async/core/timer.hpp>

using async::core::io_context;
using async::core::task;

task<void> app(io_context& ctx)
{
  co_await ctx.timers().sleep_for(std::chrono::milliseconds(100));
  ctx.stop();
}

int main()
{
  io_context ctx;
  auto t = app(ctx);
  ctx.post(t.handle());
  ctx.run();
}
```

---

## CPU-bound work

```cpp
auto result = co_await ctx.cpu_pool().submit([] {
  return heavy_computation();
});
```

Properties:

- runs on worker threads
- result safely resumes on event loop
- no user locking required

---

## Detached coroutines

```cpp
spawn_detached(ctx, handle_client(std::move(client)));
```

- fire-and-forget
- lifetime-safe
- no leaks
- no use-after-free

---

## Cancellation

```cpp
cancel_source src;
auto ct = src.token();

co_await ctx.timers().sleep_for(1s, ct);

src.request_cancel();
```

Cancellation is:

- explicit
- cooperative
- composable
- propagation-safe

---

## Networking

`async` exposes **backend-agnostic async networking APIs**.

```cpp
auto listener = async::net::make_tcp_listener(ctx);
co_await listener->async_listen({"0.0.0.0", 9090});

auto client = co_await listener->async_accept();
auto n = co_await client->async_read(buffer);
co_await client->async_write(buffer.subspan(0, n));
```

Current backend:

- Asio standalone
- dedicated network thread
- clean event loop integration

---

## Tests

```
tests/
├── core/
│   ├── task_smoke_test.cpp
│   ├── cancel_smoke_test.cpp
│   ├── scheduler_smoke_test.cpp
│   └── when_smoke_test.cpp
```

```bash
cmake -S . -B build -DASYNC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

---

## Examples

```
examples/
├── 00_hello_task.cpp
├── 01_timer.cpp
├── 02_thread_pool.cpp
└── 03_tcp_echo_server.cpp
```

```bash
cmake -S . -B build -DASYNC_BUILD_EXAMPLES=ON
cmake --build build
```

---

## Build requirements

- C++20 compiler
- CMake ≥ 3.23
- Asio (standalone, fetched automatically)

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

---

## Roadmap

- when_all / task aggregation
- structured cancellation trees
- backpressure-aware networking
- metrics and tracing hooks
- alternative I/O backends
- Windows IOCP support

---

## Philosophy

`async` is intentionally small.

It does **one thing well**:
provide a correct, explicit, deterministic async runtime core for C++.

Everything else is built **on top**, not baked in.

---

## License

MIT

