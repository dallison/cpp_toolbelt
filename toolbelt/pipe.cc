#include "toolbelt/pipe.h"
#include "absl/strings/str_format.h"

#include <unistd.h>

namespace toolbelt {

absl::StatusOr<Pipe> Pipe::Create() {
  Pipe p;
  if (absl::Status status = p.Open(); !status.ok()) {
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

absl::Status Pipe::Open() {
  int pipes[2];
  int e = ::pipe(pipes);
  if (e == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to open pipe: %s", strerror(errno)));
  }
  read_.SetFd(pipes[0]);
  write_.SetFd(pipes[1]);
  return absl::OkStatus();
}

} // namespace toolbelt