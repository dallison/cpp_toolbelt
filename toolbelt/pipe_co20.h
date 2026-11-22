// Copyright 2023-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// C++20 awaitable Pipe / SharedPtrPipe I/O.  See fd_co20.h for why these
// exist alongside the AsyncWaiter-based Pipe::Read/Write APIs.
//
//   #include "toolbelt/pipe_co20.h"
//   auto n = co_await toolbelt::await::Read(pipe, buffer, length);
//   auto ptr = co_await toolbelt::await::Read(shared_ptr_pipe);
//
// Requires a C++20 compiler and @coroutines//:co_cpp20.

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "co/coroutine_cpp20.h"
#include "toolbelt/pipe.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <new>
#include <sys/poll.h>
#include <unistd.h>

namespace toolbelt {

struct PipeCo20Access {
  static bool &ReadInProgress(Pipe &p) { return p.read_in_progress_; }
  static bool &WriteInProgress(Pipe &p) { return p.write_in_progress_; }
};

namespace await {
namespace detail {

inline ::co20::ValueTask<void> AcquireRead(Pipe &pipe) {
  while (PipeCo20Access::ReadInProgress(pipe)) {
    co_await ::co20::Yield();
  }
  PipeCo20Access::ReadInProgress(pipe) = true;
  co_return;
}

inline ::co20::ValueTask<void> AcquireWrite(Pipe &pipe) {
  while (PipeCo20Access::WriteInProgress(pipe)) {
    co_await ::co20::Yield();
  }
  PipeCo20Access::WriteInProgress(pipe) = true;
  co_return;
}

inline void ReleaseRead(Pipe &pipe) {
  PipeCo20Access::ReadInProgress(pipe) = false;
}

inline void ReleaseWrite(Pipe &pipe) {
  PipeCo20Access::WriteInProgress(pipe) = false;
}

} // namespace detail

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Read(Pipe &pipe, char *buffer, size_t length) {
  co_await detail::AcquireRead(pipe);
  struct Release {
    Pipe &p;
    ~Release() { detail::ReleaseRead(p); }
  } release{pipe};

  size_t total = 0;
  while (total < length) {
    // Match Pipe::Read: wait (which yields) before reading so writers get a
    // chance to run.  co20::Wait polls first in await_ready, so yield once to
    // keep fairness when the pipe already has data.
    co_await ::co20::Yield();
    int ready = co_await ::co20::Wait(pipe.ReadFd().Fd(), POLLIN);
    if (ready != pipe.ReadFd().Fd()) {
      co_return absl::InternalError("Interrupted");
    }
    ssize_t n = ::read(pipe.ReadFd().Fd(), buffer + total, length - total);
    if (n == 0) {
      co_return absl::InternalError("EOF");
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      co_return absl::InternalError(
          absl::StrFormat("Pipe read failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  co_return static_cast<ssize_t>(total);
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Write(Pipe &pipe, const char *buffer, size_t length) {
  co_await detail::AcquireWrite(pipe);
  struct Release {
    Pipe &p;
    ~Release() { detail::ReleaseWrite(p); }
  } release{pipe};

  size_t total = 0;
  while (total < length) {
    // co20::Wait polls in await_ready, matching Pipe::Write's PollAndWait.
    int ready = co_await ::co20::Wait(pipe.WriteFd().Fd(), POLLOUT);
    if (ready != pipe.WriteFd().Fd()) {
      co_return absl::InternalError("Interrupted");
    }
    ssize_t n = ::write(pipe.WriteFd().Fd(), buffer + total, length - total);
    if (n == 0) {
      co_return absl::InternalError("EOF");
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      co_return absl::InternalError(
          absl::StrFormat("Pipe write failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  co_return static_cast<ssize_t>(total);
}

template <typename T>
::co20::ValueTask<absl::StatusOr<std::shared_ptr<T>>>
Read(SharedPtrPipe<T> &pipe) {
  char buffer[sizeof(std::shared_ptr<T>)];
  const size_t length = sizeof(buffer);
  size_t total = 0;

  co_await detail::AcquireRead(pipe);
  struct Release {
    Pipe &p;
    ~Release() { detail::ReleaseRead(p); }
  } release{pipe};

  while (total < length) {
    co_await ::co20::Yield();
    int ready = co_await ::co20::Wait(pipe.ReadFd().Fd(), POLLIN);
    if (ready != pipe.ReadFd().Fd()) {
      co_return absl::InternalError("Interrupted");
    }
    ssize_t n = ::read(pipe.ReadFd().Fd(), buffer + total, length - total);
    if (n == 0) {
      co_return absl::InternalError("EOF");
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      co_return absl::InternalError(
          absl::StrFormat("SharedPtrPipe read failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  auto copy = *reinterpret_cast<std::shared_ptr<T> *>(buffer);
  reinterpret_cast<std::shared_ptr<T> *>(buffer)->reset();
  co_return copy;
}

template <typename T>
::co20::ValueTask<absl::Status>
Write(SharedPtrPipe<T> &pipe, std::shared_ptr<T> p) {
  char buffer[sizeof(std::shared_ptr<T>)];

  co_await detail::AcquireWrite(pipe);
  struct Release {
    Pipe &p;
    ~Release() { detail::ReleaseWrite(p); }
  } release{pipe};

  new (buffer) std::shared_ptr<T>(p);
  struct ScopedReference {
    char *buffer_;
    ~ScopedReference() {
      if (buffer_ != nullptr) {
        reinterpret_cast<std::shared_ptr<T> *>(buffer_)->reset();
      }
    }
  } sr{buffer};

  const size_t length = sizeof(buffer);
  size_t total = 0;
  while (total < length) {
    int ready = co_await ::co20::Wait(pipe.WriteFd().Fd(), POLLOUT);
    if (ready != pipe.WriteFd().Fd()) {
      co_return absl::InternalError("Interrupted");
    }
    ssize_t n = ::write(pipe.WriteFd().Fd(), buffer + total, length - total);
    if (n == 0) {
      co_return absl::InternalError("EOF");
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      co_return absl::InternalError(
          absl::StrFormat("SharedPtrPipe write failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  sr.buffer_ = nullptr;
  co_return absl::OkStatus();
}

} // namespace await
} // namespace toolbelt
