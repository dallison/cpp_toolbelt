// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_TRIGGERFD_H
#define __TOOLBELT_TRIGGERFD_H

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "toolbelt/fd.h"

namespace toolbelt {

// A TriggerFd is a file descriptor that can be used to trigger
// an event.  If the facility is available, it is implemented
// as an eventfd, otherwise it's a pipe.
class TriggerFd {
 public:
  TriggerFd() = default;

  TriggerFd(const FileDescriptor& poll_fd, const FileDescriptor& trigger_fd)
      : poll_fd_(poll_fd), trigger_fd_(trigger_fd) {}
  TriggerFd(const TriggerFd &f) = delete;
  TriggerFd(TriggerFd &&f)
      : poll_fd_(std::move(f.poll_fd_)), trigger_fd_(std::move(f.trigger_fd_)) {
    f.poll_fd_.Reset();
    f.trigger_fd_.Reset();
  }
  TriggerFd &operator=(const TriggerFd &f) = delete;
  TriggerFd &operator=(TriggerFd &&f) {
    poll_fd_ = std::move(f.poll_fd_);
    trigger_fd_ = std::move(f.trigger_fd_);
    f.poll_fd_.Reset();
    f.trigger_fd_.Reset();
    return *this;
  }

  ~TriggerFd() = default;

  absl::Status Open();

  static absl::StatusOr<TriggerFd> Create();
  static absl::StatusOr<TriggerFd> Create(const FileDescriptor& poll_fd, const FileDescriptor& trigger_fd);

  void Close() {
    poll_fd_.Close();
    trigger_fd_.Close();
  }

  void SetPollFd(FileDescriptor fd) { poll_fd_ = std::move(fd); }
  void SetTriggerFd(FileDescriptor fd) { trigger_fd_ = std::move(fd); }

  void Trigger();

  // Clears the trigger and simultaneously checks if it was triggered.
  // Returns true if the triggerfd had been triggered prior to entering this function. 
  bool Clear();

  FileDescriptor &GetPollFd() { return poll_fd_; }
  FileDescriptor &GetTriggerFd() { return trigger_fd_; }

  void AddPollFd(std::vector<struct pollfd> &fds) {
    fds.push_back({.fd = poll_fd_.Fd(), .events = 0});
  }

 private:
  FileDescriptor poll_fd_;     // File descriptor to poll on.
  FileDescriptor trigger_fd_;  // File descriptor to trigger.
};

}  // namespace subspace

#endif  // __TOOLBELT_TRIGGERFD_H
