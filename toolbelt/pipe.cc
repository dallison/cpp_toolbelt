#include "toolbelt/pipe.h"
#include "absl/strings/str_format.h"

#include <unistd.h>

namespace toolbelt {

absl::StatusOr<Pipe> Pipe::Create() { return CreateWithFlags(0); }

absl::StatusOr<Pipe> Pipe::CreateWithFlags(int flags) {
  Pipe p;
  if (absl::Status status = p.Open(flags); !status.ok()) {
    return status;
  }
  return p;
}

absl::StatusOr<Pipe> Pipe::Create(int r, int w) {
  Pipe p(r, w);
  if (absl::Status status = p.Open(); !status.ok()) {
    return status;
  }
  return p;
}

absl::Status Pipe::Open(int flags) {
  int pipes[2];
#if defined(__linux__)
  // Linux has pipe2.
  int e = ::pipe2(pipes, flags);
  if (e == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to open pipe: %s", strerror(errno)));
  }
#else
  int e = ::pipe(pipes);
  if (e == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to open pipe: %s", strerror(errno)));
  }
  // Set the flags on both ends of the pipe if non-zero.
  if (flags != 0) {
    if (fcntl(pipes[0], F_SETFL, flags) == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to set flags on read end of pipe: %s", strerror(errno)));
    }
    if (fcntl(pipes[1], F_SETFL, flags) == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to set flags on write end of pipe: %s", strerror(errno)));
    }
  }
#endif

  read_.SetFd(pipes[0]);
  write_.SetFd(pipes[1]);
  return absl::OkStatus();
}

absl::StatusOr<size_t> Pipe::GetPipeSize() {
#if defined(__linux__)
  int e = fcntl(read_.Fd(), F_GETPIPE_SZ, 0);
  if (e == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to get pipe size: %s", strerror(errno)));
  }
  return static_cast<size_t>(e);
#else
  return absl::UnimplementedError("GetPipeSize is not implemented on this OS");
#endif
}

absl::Status Pipe::SetPipeSize(size_t size) {
#if defined(__linux__)
  int e = fcntl(write_.Fd(), F_SETPIPE_SZ, size);
  if (e == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to set pipe size: %s", strerror(errno)));
  }
  return absl::OkStatus();
#else
  return absl::UnimplementedError("SetPipeSize is not implemented on this OS");
#endif
}

absl::StatusOr<ssize_t> Pipe::Read(char *buffer, size_t length,
                                   const co::Coroutine *c) {
  size_t total = 0;
  ScopedRead sc(*this, c);

  while (total < length) {
    if (c != nullptr) {
      // Coroutines do a context switch before reading.  If we use PollAndWait
      // we can get starvation.
      int fd = c->Wait(read_.Fd(), POLLIN);
      if (fd != read_.Fd()) {
        return absl::InternalError("Interrupted");
      }
    }
    ssize_t n = ::read(read_.Fd(), buffer + total, length - total);
    if (n == 0) {
      // EOF
      return absl::InternalError("EOF");
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (c == nullptr) {
          return absl::InternalError("Read would block");
        }
        if (read_.IsNonBlocking()) {
          int fd = c->Wait(read_.Fd(), POLLIN);
          if (fd != read_.Fd()) {
            return absl::InternalError("Interrupted");
          }
        }
      }
    }
    total += n;
  }
  return total;
}

absl::StatusOr<ssize_t> Pipe::Write(const char *buffer, size_t length,
                                    const co::Coroutine *c) {
  size_t total = 0;
  ScopedWrite sc(*this, c);

  while (total < length) {
    if (c != nullptr) {
      // When writing we use PollAndWait to cause the write to happen as soon as
      // possible.  This speeds up writes as there is no context switch before
      // the write.
      int fd = c->PollAndWait(write_.Fd(), POLLOUT);
      if (fd != write_.Fd()) {
        return absl::InternalError("Interrupted");
      }
    }
    ssize_t n = ::write(write_.Fd(), buffer + total, length - total);
    if (n == 0) {
      // EOF
      return absl::InternalError("EOF");
    }
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (c == nullptr) {
          return absl::InternalError("Write would block");
        }
        if (write_.IsNonBlocking()) {
          int fd = c->Wait(write_.Fd(), POLLOUT);
          if (fd != write_.Fd()) {
            return absl::InternalError("Interrupted");
          }
        }
      }
    }
    total += n;
  }
  return total;
}

} // namespace toolbelt