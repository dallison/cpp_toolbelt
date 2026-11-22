// Copyright 2023-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// C++20 awaitable socket I/O.  See fd_co20.h for why these exist alongside
// the AsyncWaiter-based Socket APIs.
//
//   #include "toolbelt/sockets_co20.h"
//   auto n = co_await toolbelt::await::Receive(socket, buffer, buflen);
//   auto client = co_await toolbelt::await::Accept(listener);
//
// Requires a C++20 compiler and @coroutines//:co_cpp20.

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "co/coroutine_cpp20.h"
#include "toolbelt/sockets.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <vector>

namespace toolbelt {
namespace await {
namespace detail {

inline ::co20::ValueTask<ssize_t>
ReceiveFully(int fd, size_t length, char *buffer, size_t buflen) {
  size_t offset = 0;
  size_t remaining = length;
  while (remaining > 0) {
    size_t readlen = std::min(remaining, buflen);
    int ready = co_await ::co20::Wait(fd, POLLIN);
    if (ready != fd) {
      co_return -1;
    }
    ssize_t n = ::recv(fd, buffer + offset, readlen, 0);
    if (n == -1) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      co_return -1;
    }
    if (n == 0) {
      co_return 0;
    }
    remaining -= static_cast<size_t>(n);
    offset += static_cast<size_t>(n);
  }
  co_return static_cast<ssize_t>(length);
}

inline ::co20::ValueTask<ssize_t>
SendFully(int fd, const char *buffer, size_t length, bool blocking) {
  size_t remaining = length;
  size_t offset = 0;
  while (remaining > 0) {
    if (blocking) {
      int ready = co_await ::co20::Wait(fd, POLLOUT);
      if (ready != fd) {
        co_return -1;
      }
    }
    ssize_t n = ::send(fd, buffer + offset, remaining, 0);
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        int ready = co_await ::co20::Wait(fd, POLLOUT);
        if (ready != fd) {
          co_return -1;
        }
        continue;
      }
      co_return -1;
    }
    if (n == 0) {
      co_return -1;
    }
    remaining -= static_cast<size_t>(n);
    offset += static_cast<size_t>(n);
  }
  co_return static_cast<ssize_t>(length);
}

} // namespace detail

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Receive(Socket &socket, char *buffer, size_t buflen) {
  if (!socket.Connected()) {
    co_return absl::InternalError("Socket is not connected");
  }
  ssize_t n = co_await detail::ReceiveFully(socket.GetFileDescriptor().Fd(),
                                            buflen, buffer, buflen);
  if (n == -1) {
    co_return absl::InternalError(absl::StrFormat(
        "Failed to read data from socket: %s", strerror(errno)));
  }
  co_return n;
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Send(Socket &socket, const char *buffer, size_t length) {
  if (!socket.Connected()) {
    co_return absl::InternalError("Socket is not connected");
  }
  ssize_t n =
      co_await detail::SendFully(socket.GetFileDescriptor().Fd(), buffer,
                                 length, socket.IsBlocking());
  if (n == -1) {
    co_return absl::InternalError(absl::StrFormat(
        "Failed to write data to socket: %s", strerror(errno)));
  }
  co_return n;
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
ReceiveMessage(Socket &socket, char *buffer, size_t buflen) {
  if (!socket.Connected()) {
    co_return absl::InternalError("Socket is not connected");
  }
  const int fd = socket.GetFileDescriptor().Fd();
  char lenbuf[4];
  ssize_t n =
      co_await detail::ReceiveFully(fd, sizeof(int32_t), lenbuf, sizeof(lenbuf));
  if (n != static_cast<ssize_t>(sizeof(lenbuf))) {
    if (n == 0) {
      co_return absl::InternalError(
          absl::StrFormat("Failed to read socket %d: socket closed", fd));
    }
    co_return absl::InternalError(absl::StrFormat(
        "Failed to read length from socket %d: %s", fd, strerror(errno)));
  }
  size_t length = ntohl(*reinterpret_cast<int32_t *>(lenbuf));
  n = co_await detail::ReceiveFully(fd, length, buffer, buflen);
  if (n == -1) {
    co_return absl::InternalError(absl::StrFormat(
        "Failed to read data from socket: %s", strerror(errno)));
  }
  co_return n;
}

inline ::co20::ValueTask<absl::StatusOr<std::vector<char>>>
ReceiveVariableLengthMessage(Socket &socket) {
  if (!socket.Connected()) {
    co_return absl::InternalError("Socket is not connected");
  }
  const int fd = socket.GetFileDescriptor().Fd();
  char lenbuf[4];
  ssize_t n =
      co_await detail::ReceiveFully(fd, sizeof(int32_t), lenbuf, sizeof(lenbuf));
  if (n != static_cast<ssize_t>(sizeof(lenbuf))) {
    if (n == 0) {
      co_return absl::InternalError(
          absl::StrFormat("Failed to read socket %d: socket closed", fd));
    }
    co_return absl::InternalError(absl::StrFormat(
        "Failed to read length from socket %d: %s", fd, strerror(errno)));
  }
  size_t length = ntohl(*reinterpret_cast<int32_t *>(lenbuf));
  std::vector<char> buffer(length);
  n = co_await detail::ReceiveFully(fd, length, buffer.data(), buffer.size());
  if (n == -1) {
    co_return absl::InternalError(absl::StrFormat(
        "Failed to read data from socket: %s", strerror(errno)));
  }
  co_return buffer;
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
SendMessage(Socket &socket, char *buffer, size_t length) {
  if (!socket.Connected()) {
    co_return absl::InternalError("Socket is not connected");
  }
  int32_t *lengthptr = reinterpret_cast<int32_t *>(buffer) - 1;
  *lengthptr = htonl(static_cast<uint32_t>(length));
  ssize_t n = co_await detail::SendFully(
      socket.GetFileDescriptor().Fd(), reinterpret_cast<char *>(lengthptr),
      length + sizeof(int32_t), socket.IsBlocking());
  if (n == -1) {
    co_return absl::InternalError(
        absl::StrFormat("Failed to write to socket: %s", strerror(errno)));
  }
  co_return n;
}

// Accept / datagram helpers wait once then call the existing blocking API.
// The fd is already ready, so passing a null AsyncWaiter is correct.

inline ::co20::ValueTask<absl::StatusOr<UnixSocket>>
Accept(const UnixSocket &listener) {
  const int fd = listener.GetFileDescriptor().Fd();
  if (!listener.GetFileDescriptor().Valid()) {
    co_return absl::InternalError("UnixSocket is not valid");
  }
  int ready = co_await ::co20::Wait(fd, POLLIN);
  if (ready != fd) {
    co_return absl::InternalError("Interrupted");
  }
  co_return listener.Accept(nullptr);
}

inline ::co20::ValueTask<absl::StatusOr<TCPSocket>>
Accept(const TCPSocket &listener) {
  const int fd = listener.GetFileDescriptor().Fd();
  if (!listener.GetFileDescriptor().Valid()) {
    co_return absl::InternalError("Socket is not valid");
  }
  int ready = co_await ::co20::Wait(fd, POLLIN);
  if (ready != fd) {
    co_return absl::InternalError("Interrupted");
  }
  co_return listener.Accept(nullptr);
}

inline ::co20::ValueTask<absl::StatusOr<VirtualStreamSocket>>
Accept(const VirtualStreamSocket &listener) {
  const int fd = listener.GetFileDescriptor().Fd();
  if (!listener.GetFileDescriptor().Valid()) {
    co_return absl::InternalError("Socket is not valid");
  }
  int ready = co_await ::co20::Wait(fd, POLLIN);
  if (ready != fd) {
    co_return absl::InternalError("Interrupted");
  }
  co_return listener.Accept(nullptr);
}

inline ::co20::ValueTask<absl::StatusOr<StreamSocket>>
Accept(const StreamSocket &listener) {
  const int fd = listener.GetFileDescriptor().Fd();
  int ready = co_await ::co20::Wait(fd, POLLIN);
  if (ready != fd) {
    co_return absl::InternalError("Interrupted");
  }
  co_return listener.Accept(nullptr);
}

inline ::co20::ValueTask<absl::Status>
SendTo(UDPSocket &socket, const InetAddress &addr, const void *buffer,
       size_t length) {
  const int fd = socket.GetFileDescriptor().Fd();
  int ready = co_await ::co20::Wait(fd, POLLOUT);
  if (ready != fd) {
    co_return absl::InternalError("Interrupted");
  }
  co_return socket.SendTo(addr, buffer, length, nullptr);
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Receive(UDPSocket &socket, void *buffer, size_t buflen) {
  const int fd = socket.GetFileDescriptor().Fd();
  int ready = co_await ::co20::Wait(fd, POLLIN);
  if (ready != fd) {
    co_return absl::InternalError("Interrupted");
  }
  co_return socket.Receive(buffer, buflen, nullptr);
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
ReceiveFrom(UDPSocket &socket, InetAddress &sender, void *buffer,
            size_t buflen) {
  const int fd = socket.GetFileDescriptor().Fd();
  int ready = co_await ::co20::Wait(fd, POLLIN);
  if (ready != fd) {
    co_return absl::InternalError("Interrupted");
  }
  co_return socket.ReceiveFrom(sender, buffer, buflen, nullptr);
}

inline ::co20::ValueTask<absl::Status>
SendFds(UnixSocket &socket, const std::vector<FileDescriptor> &fds) {
  if (!socket.Connected()) {
    co_return absl::InternalError("Socket is not connected");
  }
  constexpr size_t kMaxFds = 252;
  std::vector<char> control_buf(CMSG_SPACE(kMaxFds * sizeof(int)));
  size_t remaining_fds = fds.size();
  size_t first_fd = 0;
  const int sock_fd = socket.GetFileDescriptor().Fd();

  do {
    std::fill(control_buf.begin(), control_buf.end(), 0);
    int32_t num_fds = static_cast<int32_t>(fds.size());
    size_t fds_to_send = remaining_fds > kMaxFds ? kMaxFds : remaining_fds;
    struct iovec iov = {.iov_base = reinterpret_cast<void *>(&num_fds),
                        .iov_len = sizeof(int32_t)};
    size_t fds_size = fds_to_send * sizeof(int);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (fds_to_send > 0) {
      msg.msg_control = control_buf.data();
      msg.msg_controllen = static_cast<socklen_t>(CMSG_SPACE(fds_size));
      struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
      cmsg->cmsg_level = SOL_SOCKET;
      cmsg->cmsg_type = SCM_RIGHTS;
      cmsg->cmsg_len = CMSG_LEN(fds_size);
      int *fdptr = reinterpret_cast<int *>(CMSG_DATA(cmsg));
      for (size_t i = first_fd; i < first_fd + fds_to_send; i++) {
        *fdptr++ = fds[i].Fd();
      }
    }

    int ready = co_await ::co20::Wait(sock_fd, POLLOUT);
    if (ready != sock_fd) {
      co_return absl::InternalError("Interrupted");
    }
    int e = ::sendmsg(sock_fd, &msg, 0);
    if (e == -1) {
      co_return absl::InternalError(absl::StrFormat(
          "Failed to write fds to unix socket: %s", strerror(errno)));
    }
    remaining_fds -= fds_to_send;
    first_fd += fds_to_send;
  } while (remaining_fds > 0);
  co_return absl::OkStatus();
}

inline ::co20::ValueTask<absl::Status>
ReceiveFds(UnixSocket &socket, std::vector<FileDescriptor> &fds) {
  if (!socket.Connected()) {
    co_return absl::InternalError("Socket is not connected");
  }
  constexpr size_t kMaxFds = 252;
  std::vector<char> control_buf(CMSG_SPACE(kMaxFds * sizeof(int)));
  int32_t num_fds_received = 0;
  const int sock_fd = socket.GetFileDescriptor().Fd();

  for (;;) {
    int32_t total_fds = 0;
    size_t total_fds_bytes = 0;
    bool saw_rights = false;
    int num_fds = 0;

    while (total_fds_bytes < sizeof(total_fds)) {
      std::fill(control_buf.begin(), control_buf.end(), 0);
      struct iovec iov = {
          .iov_base = reinterpret_cast<char *>(&total_fds) + total_fds_bytes,
          .iov_len = sizeof(total_fds) - total_fds_bytes};
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
      struct msghdr msg = {.msg_iov = &iov,
                           .msg_iovlen = 1,
                           .msg_control = control_buf.data(),
                           .msg_controllen =
                               static_cast<socklen_t>(control_buf.size())};
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
      int ready = co_await ::co20::Wait(sock_fd, POLLIN);
      if (ready != sock_fd) {
        co_return absl::InternalError("Interrupted");
      }
      ssize_t n = ::recvmsg(sock_fd, &msg, 0);
      if (n == -1) {
        co_return absl::InternalError(absl::StrFormat(
            "Failed to read fds to unix socket: %s", strerror(errno)));
      }
      if (n == 0) {
        co_return absl::InternalError(
            "EOF from socket while reading fds");
      }
      total_fds_bytes += static_cast<size_t>(n);
      if ((msg.msg_flags & MSG_CTRUNC) != 0) {
        co_return absl::InternalError(
            "Control data was truncated while reading fds from unix socket");
      }
      for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
           cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
          continue;
        }
        saw_rights = true;
        if (cmsg->cmsg_len < CMSG_LEN(0)) {
          co_return absl::InternalError(absl::StrFormat(
              "Invalid SCM_RIGHTS control length %zu while reading fds",
              static_cast<size_t>(cmsg->cmsg_len)));
        }
        size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
        if (data_len % sizeof(int) != 0) {
          co_return absl::InternalError(absl::StrFormat(
              "Misaligned SCM_RIGHTS control length %zu while reading fds",
              static_cast<size_t>(cmsg->cmsg_len)));
        }
        int *fdptr = reinterpret_cast<int *>(CMSG_DATA(cmsg));
        int fds_in_message = static_cast<int>(data_len / sizeof(int));
        for (int i = 0; i < fds_in_message; i++) {
          fds.emplace_back(fdptr[i]);
        }
        num_fds += fds_in_message;
      }
    }

    if (total_fds == 0) {
      break;
    }
    if (!saw_rights && total_fds > 0) {
      co_return absl::InternalError(absl::StrFormat(
          "Expected %d fds from unix socket but received no SCM_RIGHTS message",
          total_fds));
    }
    num_fds_received += num_fds;
    if (num_fds_received >= total_fds) {
      break;
    }
  }
  co_return absl::OkStatus();
}

// StreamSocket I/O forwards to the concrete socket type.
inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Receive(StreamSocket &socket, char *buffer, size_t buflen) {
  switch (socket.BoundAddress().Type()) {
  case SocketAddress::kAddressInet:
    co_return co_await Receive(socket.GetTCPSocket(), buffer, buflen);
  case SocketAddress::kAddressVirtual:
    co_return co_await Receive(socket.GetVirtualStreamSocket(), buffer, buflen);
  case SocketAddress::kAddressUnix:
    co_return co_await Receive(socket.GetUnixSocket(), buffer, buflen);
  }
  co_return absl::InternalError("Invalid socket address");
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
Send(StreamSocket &socket, const char *buffer, size_t length) {
  switch (socket.BoundAddress().Type()) {
  case SocketAddress::kAddressInet:
    co_return co_await Send(socket.GetTCPSocket(), buffer, length);
  case SocketAddress::kAddressVirtual:
    co_return co_await Send(socket.GetVirtualStreamSocket(), buffer, length);
  case SocketAddress::kAddressUnix:
    co_return co_await Send(socket.GetUnixSocket(), buffer, length);
  }
  co_return absl::InternalError("Invalid socket address");
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
ReceiveMessage(StreamSocket &socket, char *buffer, size_t buflen) {
  switch (socket.BoundAddress().Type()) {
  case SocketAddress::kAddressInet:
    co_return co_await ReceiveMessage(socket.GetTCPSocket(), buffer, buflen);
  case SocketAddress::kAddressVirtual:
    co_return co_await ReceiveMessage(socket.GetVirtualStreamSocket(), buffer,
                                      buflen);
  case SocketAddress::kAddressUnix:
    co_return co_await ReceiveMessage(socket.GetUnixSocket(), buffer, buflen);
  }
  co_return absl::InternalError("Invalid socket address");
}

inline ::co20::ValueTask<absl::StatusOr<std::vector<char>>>
ReceiveVariableLengthMessage(StreamSocket &socket) {
  switch (socket.BoundAddress().Type()) {
  case SocketAddress::kAddressInet:
    co_return co_await ReceiveVariableLengthMessage(socket.GetTCPSocket());
  case SocketAddress::kAddressVirtual:
    co_return co_await ReceiveVariableLengthMessage(
        socket.GetVirtualStreamSocket());
  case SocketAddress::kAddressUnix:
    co_return co_await ReceiveVariableLengthMessage(socket.GetUnixSocket());
  }
  co_return absl::InternalError("Invalid socket address");
}

inline ::co20::ValueTask<absl::StatusOr<ssize_t>>
SendMessage(StreamSocket &socket, char *buffer, size_t length) {
  switch (socket.BoundAddress().Type()) {
  case SocketAddress::kAddressInet:
    co_return co_await SendMessage(socket.GetTCPSocket(), buffer, length);
  case SocketAddress::kAddressVirtual:
    co_return co_await SendMessage(socket.GetVirtualStreamSocket(), buffer,
                                   length);
  case SocketAddress::kAddressUnix:
    co_return co_await SendMessage(socket.GetUnixSocket(), buffer, length);
  }
  co_return absl::InternalError("Invalid socket address");
}

} // namespace await
} // namespace toolbelt
