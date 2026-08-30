#include "toolbelt/fd.h"

#include <limits>

namespace toolbelt {

// Close all open file descriptor for which the predicate returns true.
void CloseAllFds(std::function<bool(int)> predicate) {
  struct rlimit lim;
  int e = getrlimit(RLIMIT_NOFILE, &lim);
  if (e == 0) {
    const rlim_t int_max = static_cast<rlim_t>(std::numeric_limits<int>::max());
    for (rlim_t i = 0; i < lim.rlim_cur; ++i) {
      if (i > int_max) {
        break;
      }
      const int fd = static_cast<int>(i);
      if (fcntl(fd, F_GETFD) == 0 && predicate(fd)) {
        (void)close(fd);
      }
    }
  }
}

absl::StatusOr<ssize_t> FileDescriptor::Read(void *buffer, size_t length,
                                             const co::Coroutine *c) {
  if (length > static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
    return absl::InternalError("Read size too large");
  }
  char *buf = reinterpret_cast<char *>(buffer);
  size_t total = 0;
  while (total < length) {
    if (c != nullptr) {
      // For a coroutine we need to get it to wait for the fd to be ready.
      // This is a coroutine yield point.
      int fd = c->Wait(data_->fd, POLLIN);
      if (fd != Fd()) {
        return absl::InternalError(
            absl::StrFormat("Unexpected file descriptor from Wait: %d", fd));
      }
    }
    ssize_t n = ::read(Fd(), buf + total, length - total);
    if (n == 0) {
      break;
    }
    if (n == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (c == nullptr) {
          return absl::InternalError("Operation would block");
        }
        // If we are nonblocking yield the coroutine now.  When we
        // are resumed we can write to the socket again.
        if (!data_->nonblocking) {
          int fd = c->Wait(Fd(), POLLIN);
          if (fd != Fd()) {
            return absl::InternalError(absl::StrFormat(
                "Unexpected file descriptor from Wait: %d", fd));
          }
        }
        continue;
      }
      return absl::InternalError(
          absl::StrFormat("Read failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  return static_cast<ssize_t>(total);
}

absl::StatusOr<ssize_t> FileDescriptor::Write(const void *buffer, size_t length,
                                              const co::Coroutine *c) {
  if (length > static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
    return absl::InternalError("Write size too large");
  }
  const char *buf = reinterpret_cast<const char *>(buffer);

  size_t total = 0;
  while (total < length) {
    if (c != nullptr) {
      // For a coroutine we need to get it to wait for the fd to be ready.
      // This is a coroutine yield point.
      int fd = c->Wait(data_->fd, POLLOUT);
      if (fd != Fd()) {
        return absl::InternalError(
            absl::StrFormat("Unexpected file descriptor from Wait: %d", fd));
      }
    }
    ssize_t n = ::write(Fd(), buf + total, length - total);
    if (n == 0) {
      break;
    }
    if (n == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (c == nullptr) {
          return absl::InternalError("Operation would block");
        }
        // If we are nonblocking yield the coroutine now.  When we
        // are resumed we can write to the socket again.
        if (!data_->nonblocking) {
          int fd = c->Wait(Fd(), POLLOUT);
          if (fd != Fd()) {
            return absl::InternalError(absl::StrFormat(
                "Unexpected file descriptor from Wait: %d", fd));
          }
        }
        continue;
      }
      return absl::InternalError(
          absl::StrFormat("Write failed: %s", strerror(errno)));
    }
    total += static_cast<size_t>(n);
  }
  return static_cast<ssize_t>(total);
}

} // namespace toolbelt