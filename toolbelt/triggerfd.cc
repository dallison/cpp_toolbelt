// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "toolbelt/triggerfd.h"
#include "absl/strings/str_format.h"

// Linux has eventfd, which is the cheapest pollable trigger.  QNX pipes go
// through a single-threaded pipe service, so QNX uses a Unix domain socketpair.
// Every other POSIX system, including macOS, keeps the pipe.  A socketpair is
// slower there, and its read end always reports POLLOUT.
#include <unistd.h>
#if defined(__linux__)
#include <sys/eventfd.h>
#elif defined(__QNX__) || defined(__QNXNTO__)
#include <sys/socket.h>
#include <sys/types.h>
#else
#include <fcntl.h>
#endif

namespace toolbelt {

absl::StatusOr<TriggerFd> TriggerFd::Create() {
  TriggerFd t;
  if (absl::Status status = t.Open(); !status.ok()) {
    return status;
  }
  return t;
}

absl::StatusOr<TriggerFd> TriggerFd::Create(const FileDescriptor &poll_fd,
                                            const FileDescriptor &trigger_fd) {
  TriggerFd t(poll_fd, trigger_fd);
  if (absl::Status status = t.Open(); !status.ok()) {
    return status;
  }
  return t;
}

absl::Status TriggerFd::Open() {
#if defined(__linux__)
  int fd = eventfd(0, EFD_NONBLOCK);
  if (fd == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to create trigger fd eventfd: %s", strerror(errno)));
  }
  poll_fd_.SetFd(fd);
  trigger_fd_ = poll_fd_;
#elif defined(__QNX__) || defined(__QNXNTO__)
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to create trigger fd socketpair: %s", strerror(errno)));
  }
  // Owned immediately so both ends are closed if setting nonblocking fails.
  poll_fd_.SetFd(fds[0]);
  trigger_fd_.SetFd(fds[1]);
  if (absl::Status status = poll_fd_.SetNonBlocking(); !status.ok()) {
    return status;
  }
  if (absl::Status status = trigger_fd_.SetNonBlocking(); !status.ok()) {
    return status;
  }
#else
  int pipes[2];
  if (pipe(pipes) == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to create trigger fd pipe: %s", strerror(errno)));
  }
  // Both ends of the pipe are nonblocking.
  if (fcntl(pipes[0], F_SETFL, (fcntl(pipes[0], F_GETFL) | O_NONBLOCK)) == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to set non-blocking on read pipe: %s", strerror(errno)));
  }
  if (fcntl(pipes[1], F_SETFL, (fcntl(pipes[1], F_GETFL) | O_NONBLOCK)) == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to set non-blocking on write pipe: %s", strerror(errno)));
  }
  poll_fd_.SetFd(pipes[0]);    // Read end
  trigger_fd_.SetFd(pipes[1]); // Write end.
#endif
  return absl::OkStatus();
}

void TriggerFd::Trigger() {
#if defined(__linux__)
  // Linux eventfd.  Write an 8 byte value to trigger it.
  int64_t val = 1;
  (void)::write(trigger_fd_.Fd(), &val, 8);
#else
  // Pipe, or a socketpair on QNX.  Write a single byte to it.
  char val = 'x';
  (void)::write(trigger_fd_.Fd(), &val, 1);
#endif
}

bool TriggerFd::Clear() {
#if defined(__linux__)
  // Linux eventfd, read a single 8 byte value.  This will clear the
  // eventfd.
  int64_t val;
  ssize_t n = ::read(poll_fd_.Fd(), &val, 8);
  return (n > 0);
#else
  // Pipe, or a socketpair on QNX.  Read all pending bytes.  The fd is
  // nonblocking, so we get EAGAIN once it has been drained.
  bool was_triggered = false;
  for (;;) {
    char buffer[256];
    ssize_t n = ::read(poll_fd_.Fd(), buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    was_triggered = true;
  }
  return was_triggered;
#endif
}

} // namespace toolbelt
