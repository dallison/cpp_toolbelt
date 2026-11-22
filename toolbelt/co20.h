// Copyright 2023-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Umbrella header for C++20 awaitable toolbelt I/O.
//
// Stackful co::Coroutine can suspend inside toolbelt::AsyncWaiter::Wait /
// Yield, so the existing APIs that take an AsyncWaiter* work with it.  C++20
// stackless co20::Coroutine can only suspend at co_await; calling those same
// APIs through an AsyncWaiter* blocks the scheduler thread instead.
//
// Include this header (or the individual *_co20.h headers) and use the
// toolbelt::await::* helpers with co_await:
//
//   #include "toolbelt/co20.h"
//
//   sched.Spawn([&]() -> ::co20::Task {
//     auto n = co_await toolbelt::await::Receive(socket, buf, sizeof(buf));
//     ...
//     co_return;
//   });
//
// Requires -std=c++20 and a dependency on @coroutines//:co_cpp20
// (Bazel target //toolbelt:toolbelt_co20, depends on @coroutines//co:co_cpp20).

#include "toolbelt/fd_co20.h"
#include "toolbelt/pipe_co20.h"
#include "toolbelt/sockets_co20.h"
