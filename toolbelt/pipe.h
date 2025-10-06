#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "coroutine.h"
#include "toolbelt/fd.h"

#include <chrono>
#include <errno.h>
#include <memory>
#include <thread>
#include <unistd.h>

namespace toolbelt {

class Pipe {
public:
  static absl::StatusOr<Pipe> Create();
  static absl::StatusOr<Pipe> CreateWithFlags(int flags);
  static absl::StatusOr<Pipe> Create(int r, int w);

  Pipe() = default;
  Pipe(int r, int w) : read_(r), write_(w) {}

  virtual ~Pipe() = default;

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

  virtual absl::StatusOr<ssize_t> Read(char *buffer, size_t length,
                                       const co::Coroutine *c = nullptr);
  virtual absl::StatusOr<ssize_t> Write(const char *buffer, size_t length,
                                        const co::Coroutine *c = nullptr);

protected:
  // RAII classes for keeping coroutines from interleaving reads or writes on a
  // pipe. When using coroutines we are fine until the pipe fills up.  We can't
  // allow another coroutine to come in and write to the pipe (or read from it)
  // when the original coroutine is context switched out.
  //
  // Same applies to non-coroutine use except we block with a sleep.
  struct ScopedRead {
    ScopedRead(Pipe &p, const co::Coroutine *c) : pipe(p) {
      while (pipe.read_in_progress_) {
        if (c) {
          c->Yield();
        } else {
          if (!pipe.read_.IsNonBlocking()) {
            break;
          }
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      }
      pipe.read_in_progress_ = true;
    }

    ~ScopedRead() { pipe.read_in_progress_ = false; }
    Pipe &pipe;
  };

  struct ScopedWrite {
    ScopedWrite(Pipe &p, const co::Coroutine *c) : pipe(p) {
      while (pipe.write_in_progress_) {
        if (c) {
          c->Yield();
        } else {
          if (!pipe.write_.IsNonBlocking()) {
            break;
          }
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      }
      pipe.write_in_progress_ = true;
    }

    ~ScopedWrite() { pipe.write_in_progress_ = false; }
    Pipe &pipe;
  };
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

  // You can't use raw buffers with shared ptr pipes.
  absl::StatusOr<ssize_t> Read(char *buffer, size_t length,
                               const co::Coroutine *c = nullptr) override {
    return absl::InternalError("Not supported on SharedPtrPipe");
  }
  absl::StatusOr<ssize_t> Write(const char *buffer, size_t length,
                                const co::Coroutine *c = nullptr) override {
    return absl::InternalError("Not supported on SharedPtrPipe");
  }

  absl::StatusOr<std::shared_ptr<T>> Read(const co::Coroutine *c = nullptr) {
    char buffer[sizeof(std::shared_ptr<T>)];
    size_t length = sizeof(buffer);
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
    // Ref count = N + 1.
    auto copy = *reinterpret_cast<std::shared_ptr<T> *>(buffer);
    auto *p = reinterpret_cast<std::shared_ptr<T> *>(buffer);
    p->reset();
    return copy;
  }

  // This makes the pipe an owner of the pointer.
  absl::Status Write(std::shared_ptr<T> p, const co::Coroutine *c = nullptr) {
    // On entry, ref count for p = N
    char buffer[sizeof(std::shared_ptr<T>)];

    ScopedWrite sw(*this, c);

    // Assign the pointer to the buffer. This will increment the reference count
    // but not decrement it when the function returns, thus adding the
    // in-transit reference.
    new (buffer) std::shared_ptr<T>(p);

    struct ScopedReference {
      ScopedReference(char *buffer) : buffer_(buffer) {}
      ~ScopedReference() {
        if (buffer_ == nullptr) {
          return;
        }
        auto *ptr = reinterpret_cast<std::shared_ptr<T> *>(buffer_);
        ptr->reset();
      }
      char *buffer_;
    };

    ScopedReference sr(buffer);

    size_t total = 0;
    size_t length = sizeof(buffer);
    while (total < length) {
      if (c != nullptr) {
        // When writing we use PollAndWait to cause the write to happen as soon
        // as possible.  This speeds up writes as there is no context switch
        // before the write.
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
    // Prevent deref of pointer in buffer.
    sr.buffer_ = nullptr;
    // Ref count = N+1
    return absl::OkStatus();
  }
};

} // namespace toolbelt