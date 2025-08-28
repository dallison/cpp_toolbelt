// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_FD_H
#define __TOOLBELT_FD_H

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <iostream>
#include <sys/poll.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "coroutine.h"

namespace toolbelt {

// Close all open file descriptor for which the predicate returns true.
void CloseAllFds(std::function<bool(int)> predicate);

// This represents an open file descriptor.   It counts references to the
// OS fd and closes when all references have gone away.
//
// You may be wondering why to go to all the bother to create a reference
// counted OS file descriptor when the OS does that already.  I am treating
// the OS file descriptors as an expensive resource that shouldn't be
// copied easily.  For example, if we just put an integer fd into this
// class we would have to use 'dup' to copy it.  I think that could
// be expensive on some OSes.  Instead we keep a small struct in memory
// containing the OS fd and a reference count.  This takes the allocation
// of OS file descriptors outside of this class and therefore a bug that
// copied FileDescriptor objects around will not exhaust the OSes fd
// limits.
//
// Note: this is not thread safe, because threads are evil.
//
// Another note: don't do this:
// FileDescriptor fd1(123);
// FileDescriptor fd2(123);
// Both fd1 and fd2 refer to OS fd 123, but they are completely
// unaware of each other and when one of them goes out of scope
// the OS fd will be closed.
//
// Instead, do this:
// FileDescriptor fd1(123);
// FileDescriptor fd2(fd1);
class FileDescriptor {
public:
  FileDescriptor() = default;
  // FileDescriptor initialize with an OS fd.  Takes ownership
  // of the fd and will close it when all references go away.
  explicit FileDescriptor(int fd) : data_(std::make_shared<SharedData>(fd)) {}

  // Copy constructor, increments reference on shared data.  Very cheap.
  FileDescriptor(const FileDescriptor &f) : data_(f.data_) {}

  // Move constructor, just moves a pointer, also very cheap.
  FileDescriptor(FileDescriptor &&f) : data_(f.data_) { f.data_ = nullptr; }

  // Assignment operator.  Copies the pointer and manipulates reference counts.
  FileDescriptor &operator=(const FileDescriptor &f) {
    data_ = f.data_;
    return *this;
  }

  // Move operator. Just moves the shared data pointer and clears f.
  FileDescriptor &operator=(FileDescriptor &&f) {
    data_ = std::move(f.data_);
    return *this;
  }

  // Decrements the reference and if it goes to zero, we close the OS fd.
  ~FileDescriptor() { Close(); }

  // Close the OS fd (and free the shared data) if the references to go 0.
  void Close() { data_.reset(); }

  // Is the OS file descriptor open?
  bool IsOpen() const {
    struct stat st;
    return Valid() && fstat(data_->fd, &st) == 0;
  }

  // Is the OS fd a TTY?
  bool IsATTY() const { return Valid() && isatty(data_->fd); }

  // Current reference count.
  int RefCount() const { return data_ == nullptr ? 0 : data_.use_count(); }

  // Construct and return a struct pollfd suitable for use in ::poll.
  struct pollfd GetPollFd() {
    if (data_ == nullptr) {
      return {.fd = -1, .events = POLLIN};
    }
    return {.fd = data_->fd, .events = POLLIN};
  }

  bool operator==(const FileDescriptor &fd) const {
    if (!Valid()) {
      return !fd.Valid();
    }
    return Fd() == fd.Fd();
  }

  // True if the FileDescriptor refers to an OS fd.  Doesn't check
  // that the OS fd is actually open, for that use IsOpen().
  bool Valid() const { return data_ != nullptr && data_->fd != -1; }

  // What's the underlying OS fd (-1 for none or closed).
  int Fd() const { return data_ == nullptr ? -1 : data_->fd; }

  // Sets the OS fd.  If it's the same as the underlying OS fd, there is
  // no effect (that's not another reference to it).  Allocates new
  // shared data for the fd.
  void SetFd(int fd) {
    if (Fd() == fd) {
      // SetFd with same fd.  This isn't another reference to the
      // fd.
      return;
    }
    data_ = std::make_shared<SharedData>(fd);
  }

  void Reset() { Close(); }

  // Relinguish ownership of fd.
  void Release() {
    if (data_ != nullptr) {
      data_->fd = -1;
    }
    data_.reset();
  }

  void ForceClose() {
    if (data_ == nullptr) {
      return;
    }
    if (data_->fd != -1) {
      close(data_->fd);
      data_.reset();
    }
  }

  bool IsNonBlocking() const {
    return data_ != nullptr && data_->nonblocking;
  }

  absl::Status SetNonBlocking() {
    if (!Valid()) {
      return absl::InternalError("Cannot set nonblocking on an invalid fd");
    }
    int flags = fcntl(data_->fd, F_GETFL, 0);
    if (flags == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to set nonblocking mode on fd: %s", strerror(errno)));
    }
    int e = fcntl(data_->fd, F_SETFL, flags | O_NONBLOCK);
    if (e == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to set nonblocking mode on fd: %s", strerror(errno)));
    }
    data_->nonblocking = true;
    return absl::OkStatus();
  }

  absl::Status SetCloseOnExec() {
    if (!Valid()) {
      return absl::InternalError("Cannot set close-on-exec on an invalid fd");
    }
    int flags = fcntl(data_->fd, F_GETFD, 0);
    if (flags == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to set close-on-exec mode on fd: %s", strerror(errno)));
    }
    int e = fcntl(data_->fd, F_SETFD, flags | FD_CLOEXEC);
    if (e == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to set close-on-exec mode on fd: %s", strerror(errno)));
    }
    return absl::OkStatus();
  }

  absl::StatusOr<ssize_t> Read(void* buffer, size_t length, co::Coroutine* c = nullptr);
  absl::StatusOr<ssize_t> Write(const void* buffer, size_t length,
                                      co::Coroutine* c = nullptr);
private:
  // Reference counted OS fd, shared among all FileDescriptors with the
  // same OS fd, provided you don't create two FileDescriptors with the
  // same OS fd (that would be a mistake but there's no way to stop it).
  struct SharedData {
    SharedData() = default;
    SharedData(int f) : fd(f) {}
    ~SharedData() {
      if (fd != -1) {
        ::close(fd);
      }
    }
    int fd = -1; // OS file descriptor.
    bool nonblocking = false;
  };

  // The actual shared data.  If nullptr the FileDescriptor is invalid.
  std::shared_ptr<SharedData> data_ = nullptr;
};

} // namespace toolbelt
#endif //  __TOOLBELT_FD_H
