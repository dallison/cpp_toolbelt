// Copyright 2026 Mikael Persson
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <poll.h>

#include <chrono>
#include <limits>
#include <vector>

namespace toolbelt {

namespace toolbelt_details {

template <class Rep, class Period>
inline uint64_t ChronoToNanoseconds(std::chrono::duration<Rep, Period> duration) {
  using FloatingNanoseconds = std::chrono::duration<long double, std::nano>;
  const long double count = FloatingNanoseconds(duration).count();
  if (!(count > 0)) {
    return 0;
  }
  const long double maximum =
      static_cast<long double>(std::numeric_limits<uint64_t>::max());
  if (count >= maximum) {
    return std::numeric_limits<uint64_t>::max();
  }
  return static_cast<uint64_t>(count);
}

} // namespace toolbelt_details

// This is to provide the epoll equivalent of waiting for a set
// of pollfds
struct WaitFd {
  WaitFd(int f, uint32_t e) : fd(f), events(e) {}
  int fd;
  uint32_t events;
};

// This is a Poller. It's an abstract interface to a poll/wait/yield 
// mechanism, such as a vanilla POSIX implementation or coroutines.
class Poller {
public:
  Poller() = default;
  Poller(const Poller&) = delete;
  Poller(Poller&&) = delete;
  Poller& operator=(const Poller&) = delete;
  Poller& operator=(Poller&&) = delete;
  virtual ~Poller() = default;

  // Yield cpu time (to another thread, process, coroutine, etc.).
  virtual void Yield() const = 0;

  // For all Poll functions, there is no timeout because those are immediate
  // polls that just check current readiness status without blocking.
  // Returns -1 for no fd ready, fd if one is ready.
  // Returns -2 for an error during poll.

  // Poll a set of file descriptors with a single common event mask.
  int Poll(const std::vector<int> &fds, short event_mask = POLLIN) const {
    std::vector<struct pollfd> pfds;
    pfds.reserve(fds.size() + 1);
    for (auto &fd : fds) {
      pfds.push_back({.fd = fd, .events = short(event_mask), .revents = 0});
    }
    return PollWithMutableFds(pfds);
  }

  // Poll a set of file descriptors with their own event masks.
  // Note that the pollfd's are immutable, therefore, this function may copy
  // the vector for a typical POSIX poll call that fills in `revents`, and
  // upon return, the `revents` are not set. Use the mutable version below.
  int Poll(const std::vector<struct pollfd> &fds) const {
    std::vector<struct pollfd> pfds = fds;
    return PollWithMutableFds(pfds);
  }

  // Poll a set of file descriptors with their own event masks.
  // Upon return, the `revents` of the ready FDs will be set.
  // Note that additional FDs might be appended to this set of pollfds for
  // internal purposes.
  int Poll(std::vector<struct pollfd> &fds) const {
    return PollWithMutableFds(fds);
  }

  // For all Wait functions, the timeout is optional and if greater than zero
  // specifies a nanosecond timeout.  If the timeout occurs before the fd (or
  // one of the fds) becomes ready, Wait will return -1. If an fd is ready, Wait
  // will return the fd that terminated the wait.
  // If poll returned an error, Wait will return -2.

  // Wait for a file descriptor to become ready.  Returns the fd if it
  // was triggered or -1 for timeout.
  int Wait(int fd, uint32_t event_mask = POLLIN, uint64_t timeout_ns = 0) const {
    AddToUserWaitFds(fd, event_mask);
    return WaitOnUserWaitFds(timeout_ns);
  }

  // Wait for a set of fds, all with the same event mask.
  int Wait(const std::vector<int> &fds, uint32_t event_mask = POLLIN,
           uint64_t timeout_ns = 0) const {
    for (auto &fd : fds) {
      AddToUserWaitFds(fd, event_mask);
    }
    return WaitOnUserWaitFds(timeout_ns);
  }

  // Wait for a WaitFd.   Returns the fd if it was triggered or -1 for timeout.
  int Wait(WaitFd fd, uint64_t timeout_ns = 0) const {
    AddToUserWaitFds(fd.fd, fd.events);
    return WaitOnUserWaitFds(timeout_ns);
  }

  // Wait for a set of WaitFds.  Each needs to specify an fd and an event.
  // Returns the fd that was triggered, or -1 for a timeout.
  int Wait(const std::vector<WaitFd> &fds, uint64_t timeout_ns = 0) const {
    for (auto &fd : fds) {
      AddToUserWaitFds(fd.fd, fd.events);
    }
    return WaitOnUserWaitFds(timeout_ns);
  }
  
  // Wait for a pollfd.   Returns the fd if it was triggered or -1 for timeout.
  int Wait(struct pollfd fd, uint64_t timeout_ns = 0) const {
    AddToUserWaitFds(fd.fd, fd.events);
    return WaitOnUserWaitFds(timeout_ns);
  }

  // Wait for a set of pollfds.  Each needs to specify an fd and an event.
  // Returns the fd that was triggered, or -1 for a timeout.
  int Wait(const std::vector<struct pollfd> &fds,
           uint64_t timeout_ns = 0) const {
    for (auto &fd : fds) {
      AddToUserWaitFds(fd.fd, fd.events);
    }
    return WaitOnUserWaitFds(timeout_ns);
  }

  // For all PollAndWait functions, they combine an immediate poll to check for
  // readiness and if not, enter a wait (timed-out poll). This can be more efficient
  // in some implementation. If the timeout occurs before the fd (or
  // one of the fds) becomes ready, Wait will return -1. If an fd is ready, Wait
  // will return the fd that terminated the wait.
  // If poll returned an error, Wait will return -2.

  // Poll first and if the fd is not ready, wait for it.
  int PollAndWait(int fd, uint32_t event_mask = POLLIN,
                  uint64_t timeout_ns = 0) const {
    int n = Poll({fd}, event_mask);
    if (n != -1) {
      return n;
    }
    return Wait(fd, event_mask, timeout_ns);
  }

  // Wait for a set of fds, all with the same event mask.
  int PollAndWait(const std::vector<int> &fds, uint32_t event_mask = POLLIN,
                  uint64_t timeout_ns = 0) const {
    int n = Poll(fds, event_mask);
    if (n != -1) {
      return n;
    }
    return Wait(fds, event_mask, timeout_ns);
  }

  int PollAndWait(WaitFd fd, uint64_t timeout_ns = 0) const {
    int n = Poll({(struct pollfd){.fd = fd.fd, .events = short(fd.events)}});
    if (n != -1) {
      return fd.fd;
    }
    return Wait(fd, timeout_ns);
  }

  int PollAndWait(const std::vector<WaitFd> &fds,
                  uint64_t timeout_ns = 0) const {
    std::vector<struct pollfd> pfds;
    pfds.reserve(fds.size());
    for (auto &fd : fds) {
      pfds.push_back({.fd = fd.fd, .events = short(fd.events), .revents = 0});
    }
    int n = Poll(pfds);
    if (n != -1) {
      return n;
    }
    for (auto &fd : fds) {
      AddToUserWaitFds(fd.fd, fd.events);
    }
    return WaitOnUserWaitFds(timeout_ns);
  }

  // Wait for a pollfd.   Returns the fd if it was triggered or -1 for timeout.
  int PollAndWait(struct pollfd fd, uint64_t timeout_ns = 0) const {
    int n = Poll({fd});
    if (n != -1) {
      return fd.fd;
    }
    return Wait(fd, timeout_ns);
  }

  // Wait for a set of pollfds.  Each needs to specify an fd and an event.
  // Returns the fd that was triggered, or -1 for a timeout.
  int PollAndWait(const std::vector<struct pollfd> &fds,
                  uint64_t timeout_ns = 0) const {
    int n = Poll(fds);
    if (n != -1) {
      return n;
    }
    return Wait(fds, timeout_ns);
  }

  // Templated waits with chrono timeouts.
  template <class T, class Rep, class Period>
  int Wait(const T &fd, uint32_t events,
           std::chrono::duration<Rep, Period> duration) const {
    return Wait(
        fd, events, toolbelt_details::ChronoToNanoseconds(duration));
  }

  template <class T, class Rep, class Period>
  int Wait(const T &fd, std::chrono::duration<Rep, Period> duration) const {
    return Wait(
        fd, POLLIN, toolbelt_details::ChronoToNanoseconds(duration));
  }

  template <class T, class Rep, class Period>
  int PollAndWait(const T &fd, uint32_t events,
                  std::chrono::duration<Rep, Period> duration) const {
    return PollAndWait(
        fd, events, toolbelt_details::ChronoToNanoseconds(duration));
  }

  template <class T, class Rep, class Period>
  int PollAndWait(const T &fd,
                  std::chrono::duration<Rep, Period> duration) const {
    return PollAndWait(
        fd, POLLIN, toolbelt_details::ChronoToNanoseconds(duration));
  }

  // Sleeping functions.
  virtual void Nanosleep(uint64_t ns) const = 0;
  void Millisleep(time_t msecs) const {
    Nanosleep(static_cast<uint64_t>(msecs) * 1000000LL);
  }
  void Sleep(time_t secs) const {
    Nanosleep(static_cast<uint64_t>(secs) * 1000000000LL);
  }

  template <class Rep, class Period>
  void Sleep(std::chrono::duration<Rep, Period> duration) const {
    Nanosleep(toolbelt_details::ChronoToNanoseconds(duration));
  }

protected:
  virtual void AddToUserWaitFds(int fd, uint32_t event_mask) const = 0;
  virtual int WaitOnUserWaitFds(uint64_t timeout_ns) const = 0;
  virtual int PollWithMutableFds(std::vector<struct pollfd> &fds) const = 0;
};

class PosixPoller : public Poller {
public:
  explicit PosixPoller(uint64_t yield_sleep_ns = 0);

  void Yield() const override;
  void Nanosleep(uint64_t ns) const override;
protected:
  void AddToUserWaitFds(int fd, uint32_t event_mask) const override;
  int WaitOnUserWaitFds(uint64_t timeout_ns) const override;
  int PollWithMutableFds(std::vector<struct pollfd> &fds) const override;
private:
  uint64_t yield_sleep_ns_ = 0;
  mutable std::vector<struct pollfd> user_fds_;

  int PollImpl(std::vector<struct pollfd> &fds, int timeout_ms) const;
};

} // namespace toolbelt
