#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "coroutine.h"
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

  absl::Status SetNonBlocking(bool read, bool write) {
    if (read) {
      if (absl::Status s = read_.SetNonBlocking(); !s.ok()) {
        return s;
      }
    }
    if (write) {
      if (absl::Status s = write_.SetNonBlocking(); !s.ok()) {
        return s;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<size_t> GetPipeSize();
  absl::Status SetPipeSize(size_t size);
  
  absl::StatusOr<ssize_t> Read(char *buffer, size_t length,
                               co::Coroutine *c = nullptr);
  absl::StatusOr<ssize_t> Write(const char *buffer, size_t length,
                                co::Coroutine *c = nullptr);

protected:
  FileDescriptor read_;
  FileDescriptor write_;
  // We really don't want coroutines to interleave reads or writes on the
  // same pipe.  If they do that, the data will be interleaved and confused.
  bool read_in_progress_ = false;
  bool write_in_progress_ = false;
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

  absl::StatusOr<std::shared_ptr<T>> Read(co::Coroutine *c = nullptr) {
    char buffer[sizeof(std::shared_ptr<T>)];
    size_t length = sizeof(buffer);
    size_t total = 0;
    if (c != nullptr) {
      // If another coroutine is already reading, wait for it to finish.
      while (read_in_progress_) {
        c->Yield();
      }
      read_in_progress_ = true;
    }
    while (total < length) {
      if (c != nullptr) {
        // Coroutines do a context switch before reading.  If we use PollAndWait
        // we can get starvation.
        int fd = c->Wait(read_.Fd(), POLLIN);
        if (fd != read_.Fd()) {
          read_in_progress_ = false;
          return absl::InternalError("Interrupted");
        }
      }
      ssize_t n = ::read(read_.Fd(), buffer + total, length - total);
      if (n == 0) {
        // EOF
        read_in_progress_ = false;
        return absl::InternalError("EOF");
      }
      if (n == -1) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          if (c == nullptr) {
            read_in_progress_ = false;
            return absl::InternalError("Read would block");
          }
          if (read_.IsNonBlocking()) {
            int fd = c->Wait(read_.Fd(), POLLIN);
            if (fd != read_.Fd()) {
              read_in_progress_ = false;
              return absl::InternalError("Interrupted");
            }
          }
        }
      }
      total += n;
    }
    read_in_progress_ = false;
    // Ref count = N + 1.
    auto copy = *reinterpret_cast<std::shared_ptr<T> *>(buffer);
    auto *p = reinterpret_cast<std::shared_ptr<T> *>(buffer);
    p->reset();
    return copy;
  }

  // This makes the pipe an owner of the pointer.
  absl::Status Write(std::shared_ptr<T> p, co::Coroutine *c = nullptr) {
    // On entry, ref count for p = N
    char buffer[sizeof(std::shared_ptr<T>)];

    // Assign the pointer to the buffer. This will increment the reference count
    // but not decrement it when the function returns, thus adding the
    // in-transit reference.
    new (buffer) std::shared_ptr<T>(p);

    if (c != nullptr) {
      // If another coroutine is already writing, wait for it to finish.
      while (write_in_progress_) {
        c->Yield();
      }
      write_in_progress_ = true;
    }
    size_t total = 0;
    size_t length = sizeof(buffer);
    while (total < length) {
      if (c != nullptr) {
        // When writing we use PollAndWait to cause the write to happen as soon
        // as possible.  This speeds up writes as there is no context switch
        // before the write.
        int fd = c->PollAndWait(write_.Fd(), POLLOUT);
        if (fd != write_.Fd()) {
          write_in_progress_ = false;
          return absl::InternalError("Interrupted");
        }
      }
      ssize_t n = ::write(write_.Fd(), buffer + total, length - total);
      if (n == 0) {
        // EOF
        write_in_progress_ = false;
        return absl::InternalError("EOF");
      }
      if (n == -1) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          if (c == nullptr) {
            write_in_progress_ = false;
            return absl::InternalError("Write would block");
          }
          if (write_.IsNonBlocking()) {
            int fd = c->Wait(write_.Fd(), POLLOUT);
            if (fd != write_.Fd()) {
              write_in_progress_ = false;
              return absl::InternalError("Interrupted");
            }
          }
        }
      }
      total += n;
    }
    write_in_progress_ = false;
    // Ref count = N+1
    return absl::OkStatus();
  }
};

} // namespace toolbelt