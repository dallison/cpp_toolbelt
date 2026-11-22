// Copyright 2023-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// C++20 awaitable FileDescriptor I/O.
//
// The AsyncWaiter-based FileDescriptor::Read/Write APIs suspend stackful
// co::Coroutine instances by switching stacks inside Wait/Yield.  A C++20
// stackless coroutine can only suspend at co_await, so those calls would
// block the whole scheduler thread instead.  Use the helpers here:
//
//   #include "toolbelt/fd_co20.h"
//   auto n = co_await toolbelt::await::Read(fd, buffer, length);
//
// Requires a C++20 compiler and @coroutines//:co_cpp20.

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "co/coroutine_cpp20.h"
#include "toolbelt/fd.h"

#include <cerrno>
#include <cstring>
#include <sys/poll.h>
#include <unistd.h>

namespace toolbelt {
namespace await {

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Read(FileDescriptor &fd, void *buffer, size_t length) {
  char *buf = reinterpret_cast<char *>(buffer);
  size_t total = 0;
  while (total < length) {
    int ready = co_await ::co20::Wait(fd.Fd(), POLLIN);
    if (ready != fd.Fd()) {
      co_return absl::InternalError(
          absl::StrFormat("Unexpected file descriptor from Wait: %d", ready));
    }
    ssize_t n = ::read(fd.Fd(), buf + total, length - total);
    if (n == 0) {
      break;
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Wait above will run again on the next loop iteration.
        continue;
      }
      co_return absl::InternalError(
          absl::StrFormat("Read failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  co_return static_cast<ssize_t>(total);
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Write(FileDescriptor &fd, const void *buffer, size_t length) {
  const char *buf = reinterpret_cast<const char *>(buffer);
  size_t total = 0;
  while (total < length) {
    int ready = co_await ::co20::Wait(fd.Fd(), POLLOUT);
    if (ready != fd.Fd()) {
      co_return absl::InternalError(
          absl::StrFormat("Unexpected file descriptor from Wait: %d", ready));
    }
    ssize_t n = ::write(fd.Fd(), buf + total, length - total);
    if (n == 0) {
      break;
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      co_return absl::InternalError(
          absl::StrFormat("Write failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  co_return static_cast<ssize_t>(total);
}

} // namespace await
} // namespace toolbelt
