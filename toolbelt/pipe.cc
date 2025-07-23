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

} // namespace toolbelt