// Copyright 2026 Mikael Persson
// All Rights Reserved
// See LICENSE file for licensing information.
#include "toolbelt/poller.h"

#include <chrono>
#include <poll.h>
#include <thread>

namespace toolbelt {

PosixPoller::PosixPoller(uint64_t yield_sleep_ns) : Poller(), yield_sleep_ns_(yield_sleep_ns) {}

void PosixPoller::Yield() const {
  if (yield_sleep_ns_ == 0) {
    std::this_thread::yield();
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(yield_sleep_ns_));
  }
}

void PosixPoller::Nanosleep(uint64_t ns) const {
  std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

void PosixPoller::AddToUserWaitFds(int fd, uint32_t event_mask) const {
  user_fds_.push_back({.fd = fd, .events = short(event_mask)});
}

int PosixPoller::PollImpl(std::vector<struct pollfd> &fds, int timeout_ms) const {
  int ret = ::poll(user_fds_.data(), user_fds_.size(), timeout_ms);
  if (ret < 0) {
    return -2;
  }
  if (ret == 0) {
    return -1;
  }
  for (auto &pfd : fds) {
    if (pfd.revents & (pfd.events | POLLOUT)) {
      return pfd.fd;
    }
  }
  return -1;
}

int PosixPoller::WaitOnUserWaitFds(uint64_t timeout_ns) const {
  int timeout_ms = -1;
  if (timeout_ns != 0) {
    timeout_ms = std::max(1, static_cast<int>(timeout_ns / 1000000ULL));
  }
  int result = PollImpl(user_fds_, timeout_ms);
  user_fds_.clear();
  return result;
}

int PosixPoller::PollWithMutableFds(std::vector<struct pollfd> &fds) const {
  return PollImpl(fds, 0);
}

} // namespace toolbelt
