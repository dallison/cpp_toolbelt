#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "toolbelt/fd.h"

namespace toolbelt {

class Pipe {
public:
  static absl::StatusOr<Pipe> Create();
  static absl::StatusOr<Pipe> Create(int r, int w);

  Pipe() = default;
  Pipe(int r, int w) : read_(r), write_(w) {}

  Pipe(const Pipe&) = default;
  Pipe(Pipe&&) = default;
  Pipe& operator=(const Pipe&) = default;
  Pipe& operator=(Pipe&&) = default;

  absl::Status Open();

  FileDescriptor &ReadFd() { return read_; }
  FileDescriptor &WriteFd() { return write_; }

  void SetReadFd(int fd) { read_.SetFd(fd); }
  void SetWriteFd(int fd) { write_.SetFd(fd); }

private:
  FileDescriptor read_;
  FileDescriptor write_;
};
} // namespace toolbelt