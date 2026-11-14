// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "toolbelt/triggerfd.h"
#include "absl/strings/str_format.h"

// Linux has eventfd, which is the cheapest pollable trigger.  Everywhere else
// (macOS, QNX, other POSIX) use a Unix domain socketpair.  Pipes are a poor
// fit: on QNX they go through a single-threaded pipe service, and a socketpair
// is pollable, nonblocking, and passable over SCM_RIGHTS on every POSIX system
// we care about.
#include <unistd.h>
#if defined(__linux__)
#include <sys/eventfd.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
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
#else
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
#endif
  return absl::OkStatus();
}

void TriggerFd::Trigger() {
#if defined(__linux__)
  // Linux eventfd.  Write an 8 byte value to trigger it.
  int64_t val = 1;
  (void)::write(trigger_fd_.Fd(), &val, 8);
#else
  // Unix domain socket.  Write a single byte to it.
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
  // Unix domain socket.  Read all pending bytes.  The socket is nonblocking
  // so we get EAGAIN once it has been drained.
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
