// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "toolbelt/triggerfd.h"
#include "absl/strings/str_format.h"

#include <unistd.h>
#if TOOLBELT_TRIGGERFD_SIGNAL_MECHANISM == TOOLBELT_TRIGGERFD_EVENTFD && \
    defined(__linux__)
#define TOOLBELT_TRIGGERFD_USE_EVENTFD 1
#include <sys/eventfd.h>
#elif TOOLBELT_TRIGGERFD_SIGNAL_MECHANISM == TOOLBELT_TRIGGERFD_EVENTFD
#define TOOLBELT_TRIGGERFD_USE_EVENTFD 0
#error TOOLBELT_TRIGGERFD_EVENTFD is only available on Linux
#elif TOOLBELT_TRIGGERFD_SIGNAL_MECHANISM == TOOLBELT_TRIGGERFD_SOCKETPAIR
#define TOOLBELT_TRIGGERFD_USE_EVENTFD 0
#include <sys/socket.h>
#include <sys/types.h>
#elif TOOLBELT_TRIGGERFD_SIGNAL_MECHANISM == TOOLBELT_TRIGGERFD_PIPE
#define TOOLBELT_TRIGGERFD_USE_EVENTFD 0
// pipe() is declared in unistd.h.
#else
#define TOOLBELT_TRIGGERFD_USE_EVENTFD 0
#error TOOLBELT_TRIGGERFD_SIGNAL_MECHANISM must be TOOLBELT_TRIGGERFD_EVENTFD, TOOLBELT_TRIGGERFD_PIPE, or TOOLBELT_TRIGGERFD_SOCKETPAIR
#endif

namespace toolbelt {

#if !TOOLBELT_TRIGGERFD_USE_EVENTFD
// Opens a nonblocking pipe or socketpair. Both ends are owned by the
// FileDescriptors before either is set nonblocking, so a failure cannot leak
// a descriptor.
static absl::Status OpenByteStream(FileDescriptor &poll_fd,
                                   FileDescriptor &trigger_fd) {
  int fds[2];
#if TOOLBELT_TRIGGERFD_SIGNAL_MECHANISM == TOOLBELT_TRIGGERFD_SOCKETPAIR
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to create trigger fd socketpair: %s", strerror(errno)));
  }
#else
  if (pipe(fds) == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to create trigger fd pipe: %s", strerror(errno)));
  }
#endif
  poll_fd.SetFd(fds[0]);
  trigger_fd.SetFd(fds[1]);
  if (absl::Status status = poll_fd.SetNonBlocking(); !status.ok()) {
    poll_fd.Close();
    trigger_fd.Close();
    return status;
  }
  if (absl::Status status = trigger_fd.SetNonBlocking(); !status.ok()) {
    poll_fd.Close();
    trigger_fd.Close();
    return status;
  }
  return absl::OkStatus();
}
#endif

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
#if TOOLBELT_TRIGGERFD_USE_EVENTFD
  int fd = eventfd(0, EFD_NONBLOCK);
  if (fd == -1) {
    return absl::InternalError(absl::StrFormat(
        "Unable to create trigger fd eventfd: %s", strerror(errno)));
  }
  poll_fd_.SetFd(fd);
  trigger_fd_ = poll_fd_;
#else
  if (absl::Status status = OpenByteStream(poll_fd_, trigger_fd_);
      !status.ok()) {
    return status;
  }
#endif
  return absl::OkStatus();
}

void TriggerFd::Trigger() {
#if TOOLBELT_TRIGGERFD_USE_EVENTFD
  // Linux eventfd.  Write an 8 byte value to trigger it.
  int64_t val = 1;
  (void)::write(trigger_fd_.Fd(), &val, 8);
#else
  // Pipe or socketpair.  Write a single byte to it.
  char val = 'x';
  (void)::write(trigger_fd_.Fd(), &val, 1);
#endif
}

bool TriggerFd::Clear() {
#if TOOLBELT_TRIGGERFD_USE_EVENTFD
  // Linux eventfd, read a single 8 byte value.  This will clear the
  // eventfd.
  int64_t val;
  ssize_t n = ::read(poll_fd_.Fd(), &val, 8);
  return (n > 0);
#else
  // Pipe or socketpair.  Read all pending bytes.  The fd is nonblocking, so
  // we get EAGAIN once it has been drained.
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

#undef TOOLBELT_TRIGGERFD_USE_EVENTFD
