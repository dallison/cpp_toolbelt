#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "toolbelt/fd.h"

#include <errno.h>
#include <memory>
#include <unistd.h>

namespace toolbelt {

class Pipe {
public:
  static absl::StatusOr<Pipe> Create();
  static absl::StatusOr<Pipe> CreateWithFlags(int flags);
  static absl::StatusOr<Pipe> Create(int r, int w);

  Pipe() = default;
  Pipe(int r, int w) : read_(r), write_(w) {}

  Pipe(const Pipe &) = default;
  Pipe(Pipe &&) = default;
  Pipe &operator=(const Pipe &) = default;
  Pipe &operator=(Pipe &&) = default;

  absl::Status Open(int flags = 0);

  FileDescriptor &ReadFd() { return read_; }
  FileDescriptor &WriteFd() { return write_; }

  void SetReadFd(int fd) { read_.SetFd(fd); }
  void SetWriteFd(int fd) { write_.SetFd(fd); }

  void Close() {
    read_.Close();
    write_.Close();
  }

  void ForceClose() {
    read_.ForceClose();
    write_.ForceClose();
  }

private:
  FileDescriptor read_;
  FileDescriptor write_;
};

// A pipe that can you can use to carry a std::shared_ptr.  The pipe
// takes a reference to the pointer while it's in transit.
//
// NB: This can only be used when the sender and receiver are in the same
// process and will error out if you try to send it across the process
// boundary.
template <typename T> class SharedPtrPipe : public Pipe {
public:
  static absl::StatusOr<SharedPtrPipe<T>> Create() {
    SharedPtrPipe p;
    if (absl::Status status = p.Open(); !status.ok()) {
      return status;
    }
    return p;
  }

  static absl::StatusOr<SharedPtrPipe<T>> Create(int r, int w) {
    SharedPtrPipe p(r, w);
    if (absl::Status status = p.Open(); !status.ok()) {
      return status;
    }
    return p;
  }

  SharedPtrPipe() = default;
  SharedPtrPipe(int r, int w) : Pipe(r, w) {}

  absl::StatusOr<std::shared_ptr<T>> Read() {
    char buffer[sizeof(std::shared_ptr<T>)];
    ssize_t n = read(ReadFd().Fd(), buffer, sizeof(buffer));
    if (n <= 0) {
      return absl::InternalError(
          absl::StrFormat("Pipe read failed: %s", strerror(errno)));
    }
    // Ref count = N + 1.
    auto copy = *reinterpret_cast<std::shared_ptr<T> *>(buffer);
    auto *p = reinterpret_cast<std::shared_ptr<T> *>(buffer);
    p->reset();
    return copy;
  }

  // This makes the pipe an owner of the pointer.
  absl::Status Write(std::shared_ptr<T> p) {
    // On entry, ref count for p = N
    char buffer[sizeof(std::shared_ptr<T>)];

    // Assign the pointer to the buffer. This will increment the reference count
    // but not decrement it when the function returns, thus adding the
    // in-transit reference.
    new (buffer) std::shared_ptr<T>(p);

    // Ref count = N+1
    ssize_t n = write(WriteFd().Fd(), buffer, sizeof(buffer));
    if (n <= 0) {
      return absl::InternalError(
          absl::StrFormat("Pipe write failed: %s", strerror(errno)));
    }
    return absl::OkStatus();
  }
};

} // namespace toolbelt