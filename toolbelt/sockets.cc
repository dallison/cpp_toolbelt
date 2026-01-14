// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "sockets.h"

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <string>

#include "absl/strings/str_format.h"
#include "hexdump.h"

namespace toolbelt {

InetAddress InetAddress::BroadcastAddress(int port) {
  return InetAddress("255.255.255.255", port);
}

InetAddress InetAddress::AnyAddress(int port) { return InetAddress(port); }

InetAddress::InetAddress(const in_addr &ip, int port) {
  valid_ = true;
  addr_ = {
#if defined(__APPLE__)
      .sin_len = sizeof(struct sockaddr_in),
#endif
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = {.s_addr = ip.s_addr}};
}

InetAddress::InetAddress(int port) {
  valid_ = true;
  addr_ = {
#if defined(__APPLE__)
      .sin_len = sizeof(struct sockaddr_in),
#endif
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = {.s_addr = INADDR_ANY}};
}

InetAddress::InetAddress(const std::string &hostname, int port) {
  struct hostent *entry = gethostbyname(hostname.c_str());
  in_addr_t ipaddr;
  if (entry != NULL) {
    ipaddr = ((struct in_addr *)entry->h_addr_list[0])->s_addr;
  } else {
    // No hostname found, try IP address.
    if (inet_pton(AF_INET, hostname.c_str(), &ipaddr) != 1) {
      fprintf(stderr, "Invalid IP address or unknown hostname %s\n",
              hostname.c_str());
      valid_ = false;
      return;
    }
  }
  valid_ = true;
  addr_ = {
#if defined(__APPLE__)
      .sin_len = sizeof(struct sockaddr_in),
#endif
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = {.s_addr = ipaddr}};
}

std::string InetAddress::ToString() const {
  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
  return absl::StrFormat("%s:%d", buf, ntohs(addr_.sin_port));
}

// Virtual (vsock) addresses.

VirtualAddress::VirtualAddress(uint32_t cid, uint32_t port) {
  valid_ = true;
  memset(&addr_, 0, sizeof(addr_));
  addr_ = {
#if defined(__APPLE__)
      .svm_len = sizeof(struct sockaddr_vm),
#endif
      .svm_family = AF_VSOCK,
      .svm_port = port,
      .svm_cid = cid};
}

VirtualAddress::VirtualAddress(uint32_t port) {
  valid_ = true;
  memset(&addr_, 0, sizeof(addr_));
  addr_ = {
#if defined(__APPLE__)
      .svm_len = sizeof(struct sockaddr_vm),
#endif
      .svm_family = AF_VSOCK,
      .svm_port = port,
      .svm_cid = VMADDR_CID_ANY};
}

VirtualAddress VirtualAddress::HypervisorAddress(uint32_t port) {
  return VirtualAddress(VMADDR_CID_HYPERVISOR, port);
}

VirtualAddress VirtualAddress::HostAddress(uint32_t port) {
  return VirtualAddress(VMADDR_CID_HOST, port);
}

// Any address.
VirtualAddress VirtualAddress::AnyAddress(uint32_t port) {
  return VirtualAddress(VMADDR_CID_ANY, port);
}

#if defined(__linux__) && defined(VMADDR_CID_LOCAL)
VirtualAddress VirtualAddress::LocalAddress(uint32_t port) {
  return VirtualAddress(VMADDR_CID_LOCAL, port);
}
#endif

std::string VirtualAddress::ToString() const {
  return absl::StrFormat("%d:%d", addr_.svm_cid, addr_.svm_port);
}

static ssize_t ReceiveFully(const co::Coroutine *c, int fd, size_t length,
                            char *buffer, size_t buflen) {
  int offset = 0;
  size_t remaining = length;
  while (remaining > 0) {
    size_t readlen = std::min(remaining, buflen);
    if (c != nullptr) {
      int f = c->Wait(fd, POLLIN);
      if (f != fd) {
        return -1;
      }
    }
    ssize_t n = ::recv(fd, buffer + offset, readlen, 0);
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (c == nullptr) {
          return -1; // Prevent infinite loop for non-coroutine.
        }
        // Back to the c->Wait call to wait to try again.
        continue;
      }
      return -1;
    }
    if (n == 0) {
      // Short read.
      return 0;
    }
    remaining -= n;
    offset += n;
  }
  return length;
}

static ssize_t SendFully(const co::Coroutine *c, int fd, const char *buffer,
                         size_t length, bool blocking) {
  size_t remaining = length;
  size_t offset = 0;
  while (remaining > 0) {
    if (c != nullptr && blocking) {
      // If we are nonblocking there's no point in waiting for
      // POLLOUT before sending.  If it would block we will
      // get an EAGAIN and we will then yield the coroutine.
      // Yielding before sending to a nonblocking socket will
      // cause a context switch between coroutines and we want
      // the write to the network to be as fast as possible.
      int f = c->Wait(fd, POLLOUT);
      if (f != fd) {
        return -1;
      }
    }
    ssize_t n = ::send(fd, buffer + offset, remaining, 0);
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (c == nullptr) {
          return -1; // Prevent infinite loop for non-coroutine.
        }
        // If we are nonblocking yield the coroutine now.  When we
        // are resumed we can write to the socket again.
        if (!blocking) {
          int f = c->Wait(fd, POLLOUT);
          if (f != fd) {
            return -1;
          }
        }
        continue;
      }
      return -1;
    }
    if (n == 0) {
      // EOF on write.
      return -1;
    }
    remaining -= n;
    offset += n;
  }
  return length;
}

absl::StatusOr<ssize_t> Socket::Receive(char *buffer, size_t buflen,
                                        const co::Coroutine *c) {
  if (!Connected()) {
    return absl::InternalError("Socket is not connected");
  }

  ssize_t n = ReceiveFully(c, fd_.Fd(), buflen, buffer, buflen);
  if (n == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to read data from socket: %s", strerror(errno)));
  }
  return n;
}

absl::StatusOr<ssize_t> Socket::Send(const char *buffer, size_t length,
                                     const co::Coroutine *c) {
  if (!Connected()) {
    return absl::InternalError("Socket is not connected");
  }

  ssize_t n = SendFully(c, fd_.Fd(), buffer, length, IsBlocking());
  if (n == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to write data to socket: %s", strerror(errno)));
  }
  return n;
}

absl::StatusOr<ssize_t> Socket::ReceiveMessage(char *buffer, size_t buflen,
                                               const co::Coroutine *c) {
  if (!Connected()) {
    return absl::InternalError("Socket is not connected");
  }
  // Although the send is done using a single send to the socket by
  // prefixing it with the length, we can't use that trick for receiving.
  // We cannot avoid doing 2 receives:
  // 1. Receive the length
  // 2. Receive the data
  //
  // This is because if we receive more than the message length we will
  // be receiving data from the next message on the socket.
  char lenbuf[4];
  ssize_t n =
      ReceiveFully(c, fd_.Fd(), sizeof(int32_t), lenbuf, sizeof(lenbuf));
  if (n != sizeof(lenbuf)) {
    if (n == 0) {
      return absl::InternalError(
          absl::StrFormat("Failed to read socket %d: socket closed", fd_.Fd()));
    }
    return absl::InternalError(absl::StrFormat(
        "Failed to read length from socket %d: %s", fd_.Fd(), strerror(errno)));
  }
  size_t length = ntohl(*reinterpret_cast<int32_t *>(lenbuf));
  n = ReceiveFully(c, fd_.Fd(), length, buffer, buflen);
  if (n == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to read data from socket: %s", strerror(errno)));
  }
  return n;
}

absl::StatusOr<std::vector<char>>
Socket::ReceiveVariableLengthMessage(const co::Coroutine *c) {
  if (!Connected()) {
    return absl::InternalError("Socket is not connected");
  }
  // Although the send is done using a single send to the socket by
  // prefixing it with the length, we can't use that trick for receiving.
  // We cannot avoid doing 2 receives:
  // 1. Receive the length
  // 2. Receive the data
  //
  // This is because if we receive more than the message length we will
  // be receiving data from the next message on the socket.
  char lenbuf[4];
  ssize_t n =
      ReceiveFully(c, fd_.Fd(), sizeof(int32_t), lenbuf, sizeof(lenbuf));
  if (n != sizeof(lenbuf)) {
    if (n == 0) {
      return absl::InternalError(
          absl::StrFormat("Failed to read socket %d: socket closed", fd_.Fd()));
    }
    return absl::InternalError(absl::StrFormat(
        "Failed to read length from socket %d: %s", fd_.Fd(), strerror(errno)));
  }
  size_t length = ntohl(*reinterpret_cast<int32_t *>(lenbuf));
  std::vector<char> buffer(length);

  n = ReceiveFully(c, fd_.Fd(), length, buffer.data(), buffer.size());
  if (n == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to read data from socket: %s", strerror(errno)));
  }
  return buffer;
}

absl::StatusOr<ssize_t> Socket::SendMessage(char *buffer, size_t length,
                                            const co::Coroutine *c) {
  if (!Connected()) {
    return absl::InternalError("Socket is not connected");
  }
  // Insert length in network byte order immediately before
  // the address passed as the buffer.
  int32_t *lengthptr = reinterpret_cast<int32_t *>(buffer) - 1;
  *lengthptr = htonl(length);
  ssize_t n = SendFully(c, fd_.Fd(), reinterpret_cast<char *>(lengthptr),
                        length + sizeof(int32_t), IsBlocking());
  if (n == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to write to socket: %s", strerror(errno)));
  }
  return n;
}

// Unix Domain socket.
UnixSocket::UnixSocket() : Socket(socket(AF_UNIX, SOCK_STREAM, 0)) {}

static struct sockaddr_un BuildUnixSocketName(const std::string &pathname) {
  struct sockaddr_un addr;
  memset(reinterpret_cast<void *>(&addr), 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
#ifdef __linux__
  // On Linux we can create it in the abstract namespace which doesn't
  // consume a pathname.
  addr.sun_path[0] = '\0';
  memcpy(addr.sun_path + 1, pathname.c_str(),
         std::min(pathname.size(), sizeof(addr.sun_path) - 2));
#else
  // Portable uses the file system so it must be a valid path name.
  memcpy(addr.sun_path, pathname.c_str(),
         std::min(pathname.size(), sizeof(addr.sun_path) - 1));
#endif
  return addr;
}

static std::string ExtractUnixSocketNameString(const struct sockaddr_un &addr,
                                               socklen_t addrlen) {
#if defined(__linux__)
  auto addr_str_len =
      strnlen(addr.sun_path + 1, addrlen - offsetof(sockaddr_un, sun_path) - 1);
  return std::string(addr.sun_path + 1, addr.sun_path + addr_str_len + 1);
#else
  auto addr_str_len =
      strnlen(addr.sun_path, addrlen - offsetof(sockaddr_un, sun_path));
  return std::string(addr.sun_path, addr.sun_path + addr_str_len);
#endif
}

absl::Status UnixSocket::Bind(const std::string &pathname, bool listen) {
  struct sockaddr_un addr = BuildUnixSocketName(pathname);

  int e =
      ::bind(fd_.Fd(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
  if (e == -1) {
    fd_.Reset();
    return absl::InternalError(absl::StrFormat(
        "Failed to bind unix socket to %s: %s", pathname, strerror(errno)));
  }
  if (listen) {
    ::listen(fd_.Fd(), 10);
  }
  bound_address_ = pathname;
  return absl::OkStatus();
}

absl::StatusOr<UnixSocket> UnixSocket::Accept(const co::Coroutine *c) const {
  if (!fd_.Valid()) {
    return absl::InternalError("UnixSocket is not valid");
  }
  if (c != nullptr) {
    int fd = c->Wait(fd_.Fd(), POLLIN);
    if (fd != fd_.Fd()) {
      return absl::InternalError("Interrupted");
    }
  }
  struct sockaddr_un sender;
  socklen_t sock_len = sizeof(sender);
  int new_fd = ::accept(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&sender),
                        &sock_len);
  if (new_fd == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to accept unix socket connection on fd %d: %s",
                        fd_.Fd(), strerror(errno)));
  }
  auto new_socket = UnixSocket(new_fd, /*connected=*/true);

  struct sockaddr_un bound;
  socklen_t len = sizeof(bound);
  int e =
      getsockname(new_fd, reinterpret_cast<struct sockaddr *>(&bound), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain bound address for accepted socket: %s",
        strerror(errno)));
  }
  new_socket.bound_address_ = ExtractUnixSocketNameString(bound, len);
  return new_socket;
}

absl::Status UnixSocket::Connect(const std::string &pathname) {
  if (!fd_.Valid()) {
    return absl::InternalError("UnixSocket is not valid");
  }
  struct sockaddr_un addr = BuildUnixSocketName(pathname);

  int e = ::connect(fd_.Fd(), reinterpret_cast<const sockaddr *>(&addr),
                    sizeof(addr));
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to connect unix socket to %s: %s", pathname, strerror(errno)));
  }
  connected_ = true;
  return absl::OkStatus();
}

absl::Status UnixSocket::SendFds(const std::vector<FileDescriptor> &fds,
                                 const co::Coroutine *c) {
  if (!Connected()) {
    return absl::InternalError("Socket is not connected");
  }
  constexpr size_t kMaxFds = 252;
  union {
    char buf[CMSG_SPACE(kMaxFds * sizeof(int))];
    struct cmsghdr align;
  } u;

  // We send the total number file descriptors.  There is a limit to the
  // number we can send in one message.
  size_t remaining_fds = fds.size();
  size_t first_fd = 0;

  // We need to send at least one message, even if there are no fds to send.
  do {
    memset(u.buf, 0, sizeof(u.buf));

    int32_t num_fds = static_cast<int32_t>(fds.size());
    size_t fds_to_send = remaining_fds > kMaxFds ? kMaxFds : remaining_fds;
    struct iovec iov = {.iov_base = reinterpret_cast<void *>(&num_fds),
                        .iov_len = sizeof(int32_t)};
    size_t fds_size = fds_to_send * sizeof(int);
    struct msghdr msg = {.msg_iov = &iov,
                         .msg_iovlen = 1,
                         .msg_control = u.buf,
                         .msg_controllen =
                             static_cast<socklen_t>(CMSG_SPACE(fds_size))};

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(fds_size);
    int *fdptr = reinterpret_cast<int *>(CMSG_DATA(cmsg));
    for (size_t i = first_fd; i < first_fd + fds_to_send; i++) {
      *fdptr++ = fds[i].Fd();
    }

    if (c != nullptr) {
      int fd = c->Wait(fd_.Fd(), POLLOUT);
      if (fd != fd_.Fd()) {
        return absl::InternalError("Interrupted");
      }
    }
    int e = ::sendmsg(fd_.Fd(), &msg, 0);
    if (e == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to write fds to unix socket: %s", strerror(errno)));
    }
    remaining_fds -= fds_to_send;
    first_fd += fds_to_send;
  } while (remaining_fds > 0);
  return absl::OkStatus();
}

absl::Status UnixSocket::ReceiveFds(std::vector<FileDescriptor> &fds,
                                    const co::Coroutine *c) {
  if (!Connected()) {
    return absl::InternalError("Socket is not connected");
  }
  constexpr size_t kMaxFds = 252;
  union {
    char buf[CMSG_SPACE(kMaxFds * sizeof(int))];
    struct cmsghdr align;
  } u;

  int32_t num_fds_received = 0;
  for (;;) {
    // The total number of fds we need to see.  This is
    // sent in each message, but each message contains only portion
    // of the total (there's a limit per message).
    int32_t total_fds;
    struct iovec iov = {.iov_base = reinterpret_cast<void *>(&total_fds),
                        .iov_len = sizeof(int32_t)};

    struct msghdr msg = {.msg_iov = &iov,
                         .msg_iovlen = 1,
                         .msg_control = u.buf,
                         .msg_controllen = sizeof(u.buf)};

    if (c != nullptr) {
      int fd = c->Wait(fd_.Fd(), POLLIN);
      if (fd != fd_.Fd()) {
        return absl::InternalError("Interrupted");
      }
    }
    ssize_t n = ::recvmsg(fd_.Fd(), &msg, 0);
    if (n == -1) {
      return absl::InternalError(absl::StrFormat(
          "Failed to read fds to unix socket: %s", strerror(errno)));
    }
    if (n == 0) {
      return absl::InternalError(
          absl::StrFormat("EOF from socket while reading fds\n"));
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == nullptr) {
      // This can happen, apparently.
      return absl::OkStatus();
    }
    int *fdptr = reinterpret_cast<int *>(CMSG_DATA(cmsg));
    int num_fds = (cmsg->cmsg_len - sizeof(struct cmsghdr)) / sizeof(int);
    for (int i = 0; i < num_fds; i++) {
      fds.emplace_back(fdptr[i]);
    }
    // Add the number we received in this message to the total.
    num_fds_received += num_fds;

    // Have we received all our fds?
    if (num_fds_received >= total_fds) {
      break;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> UnixSocket::GetPeerName() const {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  struct sockaddr_un peer;
  socklen_t len = sizeof(peer);
  int e =
      getpeername(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&peer), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain peer address for socket: %s", strerror(errno)));
  }
  return ExtractUnixSocketNameString(peer, len);
}

absl::StatusOr<std::string> UnixSocket::LocalAddress() const {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  struct sockaddr_un local;
  socklen_t len = sizeof(local);
  int e =
      getsockname(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&local), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain local address for socket: %s", strerror(errno)));
  }
  return ExtractUnixSocketNameString(local, len);
}

// Network socket.
absl::Status NetworkSocket::Connect(const InetAddress &addr) {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  if (!addr.Valid()) {
    return absl::InternalError("Bad InetAddress");
  }
  int e = ::connect(fd_.Fd(),
                    reinterpret_cast<const sockaddr *>(&addr.GetAddress()),
                    addr.GetLength());
  if (e == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to connect socket to %s: %s",
                        addr.ToString().c_str(), strerror(errno)));
  }
  connected_ = true;
  return absl::OkStatus();
}

absl::Status NetworkSocket::SetReuseAddr() {
  int val = 1;
  int e = setsockopt(fd_.Fd(), SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
  if (e != 0) {
    return absl::InternalError(absl::StrFormat(
        "Unable to set SO_REUSEADDR on socket: %s", strerror(errno)));
  }
  return absl::OkStatus();
}

absl::Status NetworkSocket::SetReusePort() {
  int val = 1;
  int e = setsockopt(fd_.Fd(), SOL_SOCKET, SO_REUSEPORT, &val, sizeof(int));
  if (e != 0) {
    return absl::InternalError(absl::StrFormat(
        "Unable to set SO_REUSEPORT on socket: %s", strerror(errno)));
  }
  return absl::OkStatus();
}

// TCP socket
TCPSocket::TCPSocket() : NetworkSocket(socket(AF_INET, SOCK_STREAM, 0)) {}

absl::Status TCPSocket::Bind(const InetAddress &addr, bool listen) {
  bool binding_to_zero = addr.Port() == 0;
  int bind_err =
      ::bind(fd_.Fd(), reinterpret_cast<const sockaddr *>(&addr.GetAddress()),
             addr.GetLength());
  if (bind_err == -1) {
    fd_.Reset();
    return absl::InternalError(
        absl::StrFormat("Failed to bind TCP socket to %s: %s",
                        addr.ToString().c_str(), strerror(errno)));
  }
  bound_address_ = addr;
  if (binding_to_zero) {
    struct sockaddr_in bound;
    socklen_t len = sizeof(bound);
    int name_err = getsockname(
        fd_.Fd(), reinterpret_cast<struct sockaddr *>(&bound), &len);
    if (name_err == -1) {
      return absl::InternalError(
          absl::StrFormat("Failed to obtain bound address for %s: %s",
                          addr.ToString().c_str(), strerror(errno)));
    }
    bound_address_.SetPort(ntohs(bound.sin_port));
  }
  if (listen) {
    ::listen(fd_.Fd(), 10);
  }
  return absl::OkStatus();
}

absl::StatusOr<TCPSocket> TCPSocket::Accept(const co::Coroutine *c) const {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  if (c != nullptr) {
    int fd = c->Wait(fd_.Fd(), POLLIN);
    if (fd != fd_.Fd()) {
      return absl::InternalError("Interrupted");
    }
  }
  struct sockaddr_in sender;
  socklen_t sock_len = sizeof(sender);
  int new_fd = ::accept(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&sender),
                        &sock_len);
  auto new_socket = TCPSocket(new_fd, /*connected=*/true);
  struct sockaddr_in bound;
  socklen_t len = sizeof(bound);
  int e =
      getsockname(new_fd, reinterpret_cast<struct sockaddr *>(&bound), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain bound address for accepted socket: %s",
        strerror(errno)));
  }
  new_socket.bound_address_.SetAddress(bound);
  return new_socket;
}

absl::StatusOr<InetAddress> TCPSocket::GetPeerName() const {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  struct sockaddr_in peer;
  socklen_t len = sizeof(peer);
  int e =
      getpeername(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&peer), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain peer address for socket: %s", strerror(errno)));
  }
  return InetAddress(peer);
}

absl::StatusOr<InetAddress> TCPSocket::LocalAddress(int port) const {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  struct sockaddr_in local;
  socklen_t len = sizeof(local);
  int e =
      getsockname(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&local), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain local address for socket: %s", strerror(errno)));
  }
  return InetAddress(local);
}

// UDP socket
UDPSocket::UDPSocket()
    : NetworkSocket(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) {}

absl::Status UDPSocket::Bind(const InetAddress &addr) {
  int e =
      ::bind(fd_.Fd(), reinterpret_cast<const sockaddr *>(&addr.GetAddress()),
             addr.GetLength());
  if (e == -1) {
    fd_.Reset();
    return absl::InternalError(
        absl::StrFormat("Failed to bind UDP socket to %s: %s",
                        addr.ToString().c_str(), strerror(errno)));
  }

  bound_address_ = addr;

  if (addr.Port() == 0) {
    // An ephemeral port was request, find out what the assigned port is
    sockaddr_in address;
    socklen_t address_size = sizeof(address);
    if (getsockname(fd_.Fd(), reinterpret_cast<sockaddr *>(&address),
                    &address_size) != 0) {
      return absl::InternalError(
          absl::StrFormat("Failed to get ephemeral port assignment for %s: %s",
                          addr.ToString().c_str(), strerror(errno)));
    }
    bound_address_.SetPort(ntohs(address.sin_port));
  }

  connected_ = true; // UDP sockets are always connected when bound.
  return absl::OkStatus();
}

absl::Status UDPSocket::JoinMulticastGroup(const InetAddress &addr) {
  ip_mreqn membership_request{.imr_multiaddr = addr.GetAddress().sin_addr,
                              .imr_address = {INADDR_ANY},
                              .imr_ifindex = 0};
  int setsockopt_ret =
      ::setsockopt(fd_.Fd(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership_request,
                   sizeof(membership_request));
  if (setsockopt_ret != 0) {
    fd_.Reset();
    return absl::InternalError(
        absl::StrFormat("Failed to join multicast group %s: %s",
                        addr.ToString().c_str(), strerror(errno)));
  }
  return absl::OkStatus();
}

absl::Status UDPSocket::LeaveMulticastGroup(const InetAddress &addr) {
  ip_mreqn membership_request{.imr_multiaddr = addr.GetAddress().sin_addr,
                              .imr_address = {INADDR_ANY},
                              .imr_ifindex = 0};
  int setsockopt_ret =
      ::setsockopt(fd_.Fd(), IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   &membership_request, sizeof(membership_request));
  if (setsockopt_ret != 0) {
    fd_.Reset();
    return absl::InternalError(
        absl::StrFormat("Failed to join multicast group %s: %s",
                        addr.ToString().c_str(), strerror(errno)));
  }
  return absl::OkStatus();
}

absl::Status UDPSocket::SetBroadcast() {
  int val = 1;
  int e = setsockopt(fd_.Fd(), SOL_SOCKET, SO_BROADCAST, &val, sizeof(int));
  if (e != 0) {
    return absl::InternalError(absl::StrFormat(
        "Unable to set broadcast on UDP socket: %s", strerror(errno)));
  }
  return absl::OkStatus();
}

absl::Status UDPSocket::SetMulticastLoop() {
  constexpr int enable = 1;
  if (setsockopt(fd_.Fd(), IPPROTO_IP, IP_MULTICAST_LOOP, &enable,
                 sizeof(enable)) != 0) {
    return absl::InternalError(absl::StrFormat(
        "Unable to set multicast loop on UDP socket: %s", strerror(errno)));
  }
  return absl::OkStatus();
}

absl::Status UDPSocket::SendTo(const InetAddress &addr, const void *buffer,
                               size_t length, const co::Coroutine *c) {
  if (c != nullptr) {
    int fd = c->Wait(fd_.Fd(), POLLOUT);
    if (fd != fd_.Fd()) {
      return absl::InternalError("Interrupted");
    }
  }
  ssize_t n = ::sendto(fd_.Fd(), buffer, length, 0,
                       reinterpret_cast<const sockaddr *>(&addr.GetAddress()),
                       addr.GetLength());
  if (n == -1) {
    return absl::InternalError(
        absl::StrFormat("Unable to send UDP datagram to %s: %s",
                        addr.ToString(), strerror(errno)));
  }
  return absl::OkStatus();
}

absl::StatusOr<ssize_t> UDPSocket::Receive(void *buffer, size_t buflen,
                                           const co::Coroutine *c) {
  if (c != nullptr) {
    int fd = c->Wait(fd_.Fd(), POLLIN);
    if (fd != fd_.Fd()) {
      return absl::InternalError("Interrupted");
    }
  }
  ssize_t n = recv(fd_.Fd(), buffer, buflen, 0);
  if (n == -1) {
    return absl::InternalError(
        absl::StrFormat("Unable to receive UDP datagram: %s", strerror(errno)));
  }
  return n;
}
absl::StatusOr<ssize_t> UDPSocket::ReceiveFrom(InetAddress &sender,
                                               void *buffer, size_t buflen,
                                               const co::Coroutine *c) {
  if (c != nullptr) {
    int fd = c->Wait(fd_.Fd(), POLLIN);
    if (fd != fd_.Fd()) {
      return absl::InternalError("Interrupted");
    }
  }
  struct sockaddr_in sender_addr;
  socklen_t sender_addr_length = sizeof(sender_addr);

  ssize_t n = recvfrom(fd_.Fd(), buffer, buflen, 0,
                       reinterpret_cast<struct sockaddr *>(&sender_addr),
                       &sender_addr_length);
  if (n == -1) {
    return absl::InternalError(
        absl::StrFormat("Unable to receive UDP datagram: %s", strerror(errno)));
  }
#if defined(__APPLE__)
  sender_addr.sin_len = sender_addr_length;
#endif
  sender = {sender_addr};
  return n;
}

// Virtual (vsock) socket
VirtualStreamSocket::VirtualStreamSocket()
    : Socket(socket(AF_VSOCK, SOCK_STREAM, 0)) {}

absl::Status VirtualStreamSocket::Bind(const VirtualAddress &addr,
                                       bool listen) {
  bool binding_to_zero = addr.Port() == 0;
  int bind_err =
      ::bind(fd_.Fd(), reinterpret_cast<const sockaddr *>(&addr.GetAddress()),
             addr.GetLength());
  if (bind_err == -1) {
    fd_.Reset();
    return absl::InternalError(
        absl::StrFormat("Failed to bind TCP socket to %s: %s",
                        addr.ToString().c_str(), strerror(errno)));
  }
  bound_address_ = addr;
  if (binding_to_zero) {
    struct sockaddr_vm bound;
    socklen_t len = sizeof(bound);
    int name_err = getsockname(
        fd_.Fd(), reinterpret_cast<struct sockaddr *>(&bound), &len);
    if (name_err == -1) {
      return absl::InternalError(
          absl::StrFormat("Failed to obtain bound address for %s: %s",
                          addr.ToString().c_str(), strerror(errno)));
    }
    bound_address_.SetPort(bound.svm_port);
  }
  if (listen) {
    ::listen(fd_.Fd(), 10);
  }
  return absl::OkStatus();
}

absl::StatusOr<VirtualStreamSocket>
VirtualStreamSocket::Accept(const co::Coroutine *c) const {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  if (c != nullptr) {
    int fd = c->Wait(fd_.Fd(), POLLIN);
    if (fd != fd_.Fd()) {
      return absl::InternalError("Interrupted");
    }
  }
  struct sockaddr_vm sender;
  socklen_t sock_len = sizeof(sender);
  int new_fd = ::accept(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&sender),
                        &sock_len);
  auto new_socket = VirtualStreamSocket(new_fd, /*connected=*/true);
  struct sockaddr_vm bound;
  socklen_t len = sizeof(bound);
  int e =
      getsockname(new_fd, reinterpret_cast<struct sockaddr *>(&bound), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain bound address for accepted socket: %s",
        strerror(errno)));
  }
  new_socket.bound_address_.SetAddress(bound);
  return new_socket;
}

absl::Status VirtualStreamSocket::Connect(const VirtualAddress &addr) {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  if (!addr.Valid()) {
    return absl::InternalError("Bad VirtualAddress");
  }
  int e = ::connect(fd_.Fd(),
                    reinterpret_cast<const sockaddr *>(&addr.GetAddress()),
                    addr.GetLength());
  if (e == -1) {
    return absl::InternalError(
        absl::StrFormat("Failed to connect virtual socket to %s: %s",
                        addr.ToString().c_str(), strerror(errno)));
  }
  connected_ = true;
  return absl::OkStatus();
}

absl::StatusOr<VirtualAddress>
VirtualStreamSocket::LocalAddress(uint32_t port) const {
#if defined(IOCTL_VM_SOCKETS_GET_LOCAL_CID)
  int32_t cid;
  int e = ioctl(fd_.Fd(), IOCTL_VM_SOCKETS_GET_LOCAL_CID, &cid);
  if (e == -1) {
    return absl::InternalError("Failed to get local CID");
  }
#else
  // If we cannot get the local CID, return ANY.
  int32_t cid = VMADDR_CID_ANY;
#endif
  return VirtualAddress(cid, port);
}

absl::StatusOr<VirtualAddress> VirtualStreamSocket::GetPeerName() const {
  if (!fd_.Valid()) {
    return absl::InternalError("Socket is not valid");
  }
  struct sockaddr_vm peer;
  socklen_t len = sizeof(peer);
  int e =
      getpeername(fd_.Fd(), reinterpret_cast<struct sockaddr *>(&peer), &len);
  if (e == -1) {
    return absl::InternalError(absl::StrFormat(
        "Failed to obtain peer address for socket: %s", strerror(errno)));
  }
  return VirtualAddress(peer);
}

} // namespace toolbelt
