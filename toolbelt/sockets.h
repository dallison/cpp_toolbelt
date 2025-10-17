// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_SOCKETS_H
#define __TOOLBELT_SOCKETS_H
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "co/coroutine.h"
#include "fd.h"
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#if defined(__linux__)

#if __has_include(<linux/vm_sockets.h>)
#include <linux/vm_sockets.h>
#define HAS_VM_SOCKETS 1
#else
#define HAS_VM_SOCKETS 0
#endif

#else
#if __has_include(<sys/vsock.h>)
#include <sys/vsock.h>
#define HAS_VM_SOCKETS 1
#else
#define HAS_VM_SOCKETS 0
#endif
#endif

// Older systems may not have the header file.
#if !HAS_VM_SOCKETS
struct sockaddr_vm {
#if defined(_APPLE__)
  uint8_t svm_len;      /* total length of sockaddr */
#endif
  sa_family_t svm_family; /* AF_VSOCK */
  uint32_t svm_reserved1;
  uint32_t svm_port;
  uint32_t svm_cid;
  uint32_t svm_reserved2;
};
#define VMADDR_CID_ANY (~0U)
#define VMADDR_CID_HOST 1
#define VMADDR_CID_HYPERVISOR 2
#define VMADDR_CID_LOCAL 3
#define AF_VSOCK 40
#endif

#if !defined(VMADDR_CID_LOCAL)
#define VMADDR_CID_LOCAL 3
#endif

#include <unistd.h>
#include <variant>
#include <vector>

namespace toolbelt {

// Magical incantation to get std::visit to work.
template <class... Ts> struct EyeOfNewt : Ts... {
  using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts> EyeOfNewt(Ts...) -> EyeOfNewt<Ts...>;

// This is an internet protocol address and port.  Used as the
// endpoint address for all network based sockets.  Only IPv4 is
// supported for now.
// TODO: support IPv6.
class InetAddress {
public:
  InetAddress() = default;

  // An address with INADDR_ANY and the give port number (host order)
  InetAddress(int port);

  // An address with a given host order IP address and a host order
  // port.
  InetAddress(const in_addr &ip, int port);

  // An address for a given hostname and port.  Performs name server lookup.
  InetAddress(const std::string &hostname, int port);

  // An address from a pre-constructed socket address in network order.
  InetAddress(const struct sockaddr_in &addr) : addr_(addr), valid_(true) {}

  const sockaddr_in &GetAddress() const { return addr_; }
  socklen_t GetLength() const { return sizeof(addr_); }
  bool Valid() const { return valid_; }

  // IP address and port are returned in host byte order.
  in_addr IpAddress() const { return {ntohl(addr_.sin_addr.s_addr)}; }
  int Port() const { return ntohs(addr_.sin_port); }

  // Port is in host byte order.
  void SetPort(int port) { addr_.sin_port = htons(port); }

  void SetAddress(const struct sockaddr_in &addr) { addr_ = addr; }

  std::string ToString() const;

  // Provide support for Abseil hashing.
  friend bool operator==(const InetAddress &a, const InetAddress &b);
  template <typename H> friend H AbslHashValue(H h, const InetAddress &a);

  static InetAddress BroadcastAddress(int port);
  static InetAddress AnyAddress(int port);

private:
  struct sockaddr_in addr_ = {}; // In network byte order.
  bool valid_ = false;
};

inline bool operator==(const InetAddress &a, const InetAddress &b) {
  return memcmp(&a.addr_, &b.addr_, sizeof(a.addr_)) == 0;
}

template <typename H> inline H AbslHashValue(H h, const InetAddress &a) {
  return H::combine_contiguous(
      std::move(h), reinterpret_cast<const char *>(&a.addr_), sizeof(a.addr_));
}

// This is a virtual socket address and port.  It's used by VirtualStreamSocket
// which is implemented using the 'vsock' protocol.
class VirtualAddress {
public:
  VirtualAddress() = default;

  // An address with VMADDR_CID_ANY and the given port number
  VirtualAddress(uint32_t port);

  VirtualAddress(uint32_t cid, uint32_t port);

  VirtualAddress(const struct sockaddr_vm &addr) : addr_(addr), valid_(true) {}

  const sockaddr_vm &GetAddress() const { return addr_; }
  socklen_t GetLength() const { return sizeof(addr_); }
  bool Valid() const { return valid_; }

  uint32_t Cid() const { return addr_.svm_cid; }
  uint32_t Port() const { return addr_.svm_port; }

  void SetPort(uint32_t port) { addr_.svm_port = port; }

  void SetAddress(const struct sockaddr_vm &addr) { addr_ = addr; }

  std::string ToString() const;

  // Provide support for Abseil hashing.
  friend bool operator==(const VirtualAddress &a, const VirtualAddress &b);
  template <typename H> friend H AbslHashValue(H h, const VirtualAddress &a);

  static VirtualAddress HypervisorAddress(uint32_t port);
  static VirtualAddress HostAddress(uint32_t port);
  static VirtualAddress AnyAddress(uint32_t port);
#if defined(__linux__)
  static VirtualAddress LocalAddress(uint32_t port);
#endif

private:
  struct sockaddr_vm addr_;
  bool valid_ = false;
};

inline bool operator==(const VirtualAddress &a, const VirtualAddress &b) {
  return memcmp(&a.addr_, &b.addr_, sizeof(a.addr_)) == 0;
}

template <typename H> inline H AbslHashValue(H h, const VirtualAddress &a) {
  return H::combine_contiguous(
      std::move(h), reinterpret_cast<const char *>(&a.addr_), sizeof(a.addr_));
}

// Socket address that may be either InetAddress, VirtualAddress or Unix
// (string).
class SocketAddress {
public:
  static constexpr int kAddressInet = 0;
  static constexpr int kAddressVirtual = 1;
  static constexpr int kAddressUnix = 2;

  SocketAddress() = default;
  SocketAddress(const InetAddress &addr) : address_(addr) {}
  SocketAddress(const VirtualAddress &addr) : address_(addr) {}
  SocketAddress(const std::string &addr) : address_(addr) {}

  SocketAddress(const struct sockaddr_in &addr) : address_(InetAddress(addr)) {}

  SocketAddress(const in_addr &addr, int port)
      : address_(InetAddress(addr, port)) {}

  SocketAddress(const struct sockaddr_vm &addr)
      : address_(VirtualAddress(addr)) {}

  SocketAddress(uint32_t cid, uint32_t port)
      : address_(VirtualAddress(cid, port)) {}

  // Create a SocketAddress from a variant index and address.
  SocketAddress(int type, const void *addr) {
    switch (type) {
    case kAddressInet:
      address_ =
          InetAddress(*reinterpret_cast<const struct sockaddr_in *>(addr));
      break;
    case kAddressVirtual:
      address_ =
          VirtualAddress(*reinterpret_cast<const struct sockaddr_vm *>(addr));
      break;
    case kAddressUnix:
      address_ = std::string(reinterpret_cast<const char *>(addr));
      break;
    default:
      throw std::invalid_argument("Invalid socket address type");
    }
  }

  // Assignment operators
  SocketAddress &operator=(const InetAddress &addr) {
    address_ = addr;
    return *this;
  }
  SocketAddress &operator=(const VirtualAddress &addr) {
    address_ = addr;
    return *this;
  }
  SocketAddress &operator=(const std::string &addr) {
    address_ = addr;
    return *this;
  }

  static SocketAddress AnyPort(const SocketAddress &addr) {
    return std::visit(
        EyeOfNewt{
            [](const InetAddress &a) {
              return SocketAddress(a.IpAddress(), 0);
            },
            [](const VirtualAddress &a) { return SocketAddress(a.Cid(), 0); },
            [](const std::string &a) { return SocketAddress(a); }},
        addr.address_);
  }

  const InetAddress &GetInetAddress() const {
    return std::get<InetAddress>(address_);
  }

  const VirtualAddress &GetVirtualAddress() const {
    return std::get<VirtualAddress>(address_);
  }

  const std::string &GetUnixAddress() const {
    return std::get<std::string>(address_);
  }

  std::string ToString() const {
    return std::visit(
        EyeOfNewt{[](const InetAddress &a) { return a.ToString(); },
                  [](const VirtualAddress &a) { return a.ToString(); },
                  [](const std::string &a) { return a; }},
        address_);
  }

  bool Valid() const {
    return std::visit(
        EyeOfNewt{[](const InetAddress &a) { return a.Valid(); },
                  [](const VirtualAddress &a) { return a.Valid(); },
                  [](const std::string &a) { return !a.empty(); }},
        address_);
  }

  // What address type is in the variant.
  int Type() const { return address_.index(); }

  int Port() const {
    return std::visit(
        EyeOfNewt{[](const InetAddress &a) { return int(a.Port()); },
                  [](const VirtualAddress &a) { return int(a.Port()); },
                  [](const std::string &a) { return 0; }},
        address_);
  }

  // Provide support for Abseil hashing.
  friend bool operator==(const SocketAddress &a, const SocketAddress &b);
  template <typename H> friend H AbslHashValue(H h, const SocketAddress &a);

private:
  std::variant<InetAddress, VirtualAddress, std::string> address_;
};

inline bool operator==(const SocketAddress &a, const SocketAddress &b) {
  switch (a.address_.index()) {
  case 0:
    return std::get<InetAddress>(a.address_) ==
           std::get<InetAddress>(b.address_);
  case 1:
    return std::get<VirtualAddress>(a.address_) ==
           std::get<VirtualAddress>(b.address_);
  case 2:
    return std::get<std::string>(a.address_) ==
           std::get<std::string>(b.address_);
  default:
    return false;
  }
}

template <typename H> inline H AbslHashValue(H h, const SocketAddress &a) {
  return std::visit(EyeOfNewt{[&h](const InetAddress &a) {
                                return AbslHashValue(std::move(h), a);
                              },
                              [&h](const VirtualAddress &a) {
                                return AbslHashValue(std::move(h), a);
                              },
                              [&h](const std::string &a) {
                                return AbslHashValue(std::move(h),
                                                     std::string_view(a));
                              }},
                    a.address_);
}

// This is a general socket initialized with a file descriptor.  Subclasses
// implement the different socket types.
class Socket {
protected:
  Socket() = default;
  explicit Socket(int fd, bool connected = false)
      : fd_(fd), connected_(connected) {}
  Socket(const Socket &s) = default;
  Socket(Socket &&s) : fd_(std::move(s.fd_)), connected_(s.connected_) {
    s.connected_ = false;
  }
  Socket &operator=(const Socket &s) = default;
  Socket &operator=(Socket &&s) {
    fd_ = std::move(s.fd_);
    connected_ = s.connected_;
    s.connected_ = false;
    return *this;
  }
  ~Socket() {}
public:

  void Close() {
    fd_.Close();
    connected_ = false;
  }
  bool Connected() const { return fd_.Valid() && connected_; }

  // Send and receive raw buffers.
  absl::StatusOr<ssize_t> Receive(char *buffer, size_t buflen,
                                  const co::Coroutine *c = nullptr);
  absl::StatusOr<ssize_t> Send(const char *buffer, size_t length,
                               const co::Coroutine *c = nullptr);
  // Send and receive length-delimited message.  The length is a 4-byte
  // network byte order (big endian) int as the first 4 bytes and
  // contains the length of the message.
  absl::StatusOr<ssize_t> ReceiveMessage(char *buffer, size_t buflen,
                                         const co::Coroutine *c = nullptr);

  absl::StatusOr<std::vector<char>>
  ReceiveVariableLengthMessage(const co::Coroutine *c = nullptr);

  // For SendMessage, the buffer pointer must be 4 bytes beyond
  // the actual buffer start, which must be length+4 bytes
  // long.  We write exactly length+4 bytes to the socket starting
  // at buffer-4.  This is to allow us to do a single send
  // to the socket rather than splitting it into 2.
  absl::StatusOr<ssize_t> SendMessage(char *buffer, size_t length,
                                      const co::Coroutine *c = nullptr);

  absl::Status SetNonBlocking() {
    if (absl::Status s = fd_.SetNonBlocking(); !s.ok()) {
      return s;
    }
    is_nonblocking_ = true;
    return absl::OkStatus();
  }
  // Get the fd on which to poll for non-blocking operations.
  FileDescriptor GetFileDescriptor() const { return fd_; }

  absl::Status SetCloseOnExec() { return fd_.SetCloseOnExec(); }

  bool IsNonBlocking() const { return is_nonblocking_; }
  bool IsBlocking() const { return !is_nonblocking_; }

protected:
  FileDescriptor fd_;
  bool connected_ = false;
  bool is_nonblocking_ = false;
};

// A Unix Domain socket bound to a pathname.  Depending on the OS, this
// may or may not be in the file system.
class UnixSocket : public Socket {
public:
  UnixSocket();
  explicit UnixSocket(int fd, bool connected = false) : Socket(fd, connected) {}

  absl::Status Bind(const std::string &pathname, bool listen);
  absl::Status Connect(const std::string &pathname);

  absl::StatusOr<UnixSocket> Accept(const co::Coroutine *c = nullptr) const;

  absl::Status SendFds(const std::vector<FileDescriptor> &fds,
                       const co::Coroutine *c = nullptr);
  absl::Status ReceiveFds(std::vector<FileDescriptor> &fds,
                          const co::Coroutine *c = nullptr);

  std::string BoundAddress() const { return bound_address_; }
  absl::StatusOr<std::string> GetPeerName() const;
  absl::StatusOr<std::string> LocalAddress() const;

private:
  std::string bound_address_;
};

// A socket for communication across the network.  This is the base
// class for UDP and TCP sockets.
class NetworkSocket : public Socket {
protected:
  NetworkSocket() = default;
  explicit NetworkSocket(int fd, bool connected = false)
      : Socket(fd, connected) {}
public:

  absl::Status Connect(const InetAddress &addr);

  const InetAddress &BoundAddress() const { return bound_address_; }

  absl::Status SetReuseAddr();
  absl::Status SetReusePort();

protected:
  InetAddress bound_address_;
};

// A socket that uses the UDP datagram protocol.
class UDPSocket : public NetworkSocket {
public:
  UDPSocket();
  explicit UDPSocket(int fd, bool connected = false)
      : NetworkSocket(fd, connected) {}

  absl::Status Bind(const InetAddress &addr);

  absl::Status JoinMulticastGroup(const InetAddress &addr);
  absl::Status LeaveMulticastGroup(const InetAddress &addr);

  // NOTE: Read and Write may or may not work on UDP sockets.  Use SendTo and
  // Receive for datagrams.
  absl::Status SendTo(const InetAddress &addr, const void *buffer,
                      size_t length, const co::Coroutine *c = nullptr);
  absl::StatusOr<ssize_t> Receive(void *buffer, size_t buflen,
                                  const co::Coroutine *c = nullptr);
  absl::StatusOr<ssize_t> ReceiveFrom(InetAddress &sender, void *buffer,
                                      size_t buflen,
                                      const co::Coroutine *c = nullptr);
  absl::Status SetBroadcast();
  absl::Status SetMulticastLoop();
};

// A TCP based socket.
class TCPSocket : public NetworkSocket {
public:
  TCPSocket();
  explicit TCPSocket(int fd, bool connected = false)
      : NetworkSocket(fd, connected) {}

  absl::Status Bind(const InetAddress &addr, bool listen);

  absl::StatusOr<TCPSocket> Accept(const co::Coroutine *c = nullptr) const;

  absl::StatusOr<InetAddress> LocalAddress(int port) const;

  absl::StatusOr<InetAddress> GetPeerName() const;
};

class VirtualStreamSocket : public Socket {
public:
  VirtualStreamSocket();
  explicit VirtualStreamSocket(int fd, bool connected = false)
      : Socket(fd, connected) {}

  absl::Status Connect(const VirtualAddress &addr);

  absl::Status Bind(const VirtualAddress &addr, bool listen);

  absl::StatusOr<VirtualStreamSocket> Accept(const co::Coroutine *c = nullptr) const;
  absl::StatusOr<VirtualAddress> LocalAddress(uint32_t port) const;

  const VirtualAddress &BoundAddress() const { return bound_address_; }
  absl::StatusOr<VirtualAddress> GetPeerName() const;

  uint32_t Cid() const { return bound_address_.Cid(); }

protected:
  VirtualAddress bound_address_;
};

// Class that wraps the various stream-based sockets.  This is a compile-time
// switchable class that can represent different types of stream sockets.
class StreamSocket {
public:
  StreamSocket() = default;
  StreamSocket(const StreamSocket &s) = default;
  StreamSocket(StreamSocket &&s) = default;
  ~StreamSocket() = default;
  StreamSocket &operator=(const StreamSocket &s) = default;
  StreamSocket &operator=(StreamSocket &&s) = default;

  // Binders for TCP, Virtual, and Unix sockets.
  //
  // NOTE: I wanted to use std::visit for the Bind and Connect functions but I
  // always get an error: "cannot deduce return type 'auto' from returned value
  // of type '<EyeOfNewt function type>'" which I do not understand.  Perhaps
  // it's because I am using the SocketAddress as the variant.  Dunno.
  absl::Status Bind(const SocketAddress &addr, bool listen) {
    switch (addr.Type()) {
    case SocketAddress::kAddressInet:
      socket_ = TCPSocket();
      return std::get<TCPSocket>(socket_).Bind(addr.GetInetAddress(), listen);
    case SocketAddress::kAddressVirtual:
      socket_ = VirtualStreamSocket();
      return std::get<VirtualStreamSocket>(socket_).Bind(
          addr.GetVirtualAddress(), listen);
    case SocketAddress::kAddressUnix:
      socket_ = UnixSocket();
      return std::get<UnixSocket>(socket_).Bind(addr.GetUnixAddress(), listen);
    }
    return absl::Status(absl::StatusCode::kInternal, "Invalid socket address");
  }

  absl::Status Connect(const SocketAddress &addr) {
    switch (addr.Type()) {
    case SocketAddress::kAddressInet:
      socket_ = TCPSocket();
      return std::get<TCPSocket>(socket_).Connect(addr.GetInetAddress());
    case SocketAddress::kAddressVirtual:
      socket_ = VirtualStreamSocket();
      return std::get<VirtualStreamSocket>(socket_).Connect(
          addr.GetVirtualAddress());
    case SocketAddress::kAddressUnix:
      socket_ = UnixSocket();
      return std::get<UnixSocket>(socket_).Connect(addr.GetUnixAddress());
    }
    return absl::Status(absl::StatusCode::kInternal, "Invalid socket address");
  }

  absl::StatusOr<StreamSocket> Accept(const co::Coroutine *c = nullptr) const {
    return std::visit(
        EyeOfNewt{
            [&](const TCPSocket &s) mutable -> absl::StatusOr<StreamSocket> {
              auto status = s.Accept(c);
              if (!status.ok()) {
                return status.status();
              }
              return StreamSocket(std::move(*status));
            },
            [&](const VirtualStreamSocket &s) mutable
                -> absl::StatusOr<StreamSocket> {
              auto status = s.Accept(c);
              if (!status.ok()) {
                return status.status();
              }
              return StreamSocket(std::move(*status));
            },
            [&](const UnixSocket &s) mutable -> absl::StatusOr<StreamSocket> {
              auto status = s.Accept(c);
              if (!status.ok()) {
                return status.status();
              }
              return StreamSocket(std::move(*status));
            }},
        socket_);
  }

  // Accessors for the underlying socket types.
  TCPSocket &GetTCPSocket() { return std::get<TCPSocket>(socket_); }
  VirtualStreamSocket &GetVirtualStreamSocket() {
    return std::get<VirtualStreamSocket>(socket_);
  }
  UnixSocket &GetUnixSocket() { return std::get<UnixSocket>(socket_); }

  SocketAddress BoundAddress() const {
    return std::visit(
        EyeOfNewt{[](const TCPSocket &s) -> SocketAddress {
                    return SocketAddress(s.BoundAddress());
                  },
                  [](const VirtualStreamSocket &s) -> SocketAddress {
                    return SocketAddress(s.BoundAddress());
                  },
                  [](const UnixSocket &s) -> SocketAddress {
                    return SocketAddress(s.BoundAddress());
                  }},
        socket_);
  }

  void Close() {
    std::visit(EyeOfNewt{[](TCPSocket &s) { s.Close(); },
                         [](VirtualStreamSocket &s) { s.Close(); },
                         [](UnixSocket &s) { s.Close(); }},
               socket_);
  }

  bool Connected() const {
    return std::visit(
        EyeOfNewt{[](const TCPSocket &s) { return s.Connected(); },
                  [](const VirtualStreamSocket &s) { return s.Connected(); },
                  [](const UnixSocket &s) { return s.Connected(); }},
        socket_);
  }

  // Send and receive raw buffers.
  absl::StatusOr<ssize_t> Receive(char *buffer, size_t buflen,
                                  const co::Coroutine *c = nullptr) {
    return std::visit(
        EyeOfNewt{[&](TCPSocket &s) { return s.Receive(buffer, buflen, c); },
                  [&](VirtualStreamSocket &s) {
                    return s.Receive(buffer, buflen, c);
                  },
                  [&](UnixSocket &s) { return s.Receive(buffer, buflen, c); }},
        socket_);
  }

  absl::StatusOr<ssize_t> Send(const char *buffer, size_t length,
                               const co::Coroutine *c = nullptr) {
    return std::visit(
        EyeOfNewt{
            [&](TCPSocket &s) { return s.Send(buffer, length, c); },
            [&](VirtualStreamSocket &s) { return s.Send(buffer, length, c); },
            [&](UnixSocket &s) { return s.Send(buffer, length, c); }},
        socket_);
  }

  // Send and receive length-delimited message.  The length is a 4-byte
  // network byte order (big endian) int as the first 4 bytes and
  // contains the length of the message.
  absl::StatusOr<ssize_t> ReceiveMessage(char *buffer, size_t buflen,
                                         const co::Coroutine *c = nullptr) {
    return std::visit(
        EyeOfNewt{
            [&](TCPSocket &s) { return s.ReceiveMessage(buffer, buflen, c); },
            [&](VirtualStreamSocket &s) {
              return s.ReceiveMessage(buffer, buflen, c);
            },
            [&](UnixSocket &s) { return s.ReceiveMessage(buffer, buflen, c); }},
        socket_);
  }

  absl::StatusOr<std::vector<char>>
  ReceiveVariableLengthMessage(const co::Coroutine *c = nullptr) {
    return std::visit(
        EyeOfNewt{
            [&](TCPSocket &s) { return s.ReceiveVariableLengthMessage(c); },
            [&](VirtualStreamSocket &s) {
              return s.ReceiveVariableLengthMessage(c);
            },
            [&](UnixSocket &s) { return s.ReceiveVariableLengthMessage(c); }},
        socket_);
  }

  // For SendMessage, the buffer pointer must be 4 bytes beyond
  // the actual buffer start, which must be length+4 bytes
  // long.  We write exactly length+4 bytes to the socket starting
  // at buffer-4.  This is to allow us to do a single send
  // to the socket rather than splitting it into 2.
  absl::StatusOr<ssize_t> SendMessage(char *buffer, size_t length,
                                      const co::Coroutine *c = nullptr) {
    return std::visit(
        EyeOfNewt{
            [&](TCPSocket &s) { return s.SendMessage(buffer, length, c); },
            [&](VirtualStreamSocket &s) {
              return s.SendMessage(buffer, length, c);
            },
            [&](UnixSocket &s) { return s.SendMessage(buffer, length, c); }},
        socket_);
  }

  absl::Status SetNonBlocking() {
    return std::visit(
        EyeOfNewt{[&](TCPSocket &s) { return s.SetNonBlocking(); },
                  [&](VirtualStreamSocket &s) { return s.SetNonBlocking(); },
                  [&](UnixSocket &s) { return s.SetNonBlocking(); }},
        socket_);
  }

  // Get the fd on which to poll for non-blocking operations.
  FileDescriptor GetFileDescriptor() const {
    return std::visit(
        EyeOfNewt{
            [](const TCPSocket &s) { return s.GetFileDescriptor(); },
            [](const VirtualStreamSocket &s) { return s.GetFileDescriptor(); },
            [](const UnixSocket &s) { return s.GetFileDescriptor(); }},
        socket_);
  }

  absl::Status SetCloseOnExec() {
    return std::visit(
        EyeOfNewt{[](TCPSocket &s) { return s.SetCloseOnExec(); },
                  [](VirtualStreamSocket &s) { return s.SetCloseOnExec(); },
                  [](UnixSocket &s) { return s.SetCloseOnExec(); }},
        socket_);
  }

  bool IsNonBlocking() const {
    return std::visit(
        EyeOfNewt{
            [](const TCPSocket &s) { return s.IsNonBlocking(); },
            [](const VirtualStreamSocket &s) { return s.IsNonBlocking(); },
            [](const UnixSocket &s) { return s.IsNonBlocking(); }},
        socket_);
  }

  bool IsBlocking() const { return !IsNonBlocking(); }

  absl::StatusOr<SocketAddress> GetPeerName() const {
    return std::visit(
        EyeOfNewt{
            [&](const TCPSocket &s) -> absl::StatusOr<SocketAddress> {
              auto st = s.GetPeerName();
              if (!st.ok()) {
                return st;
              }
              return SocketAddress(*st);
            },
            [&](const VirtualStreamSocket &s) -> absl::StatusOr<SocketAddress> {
              auto st = s.GetPeerName();
              if (!st.ok()) {
                return st;
              }
              return SocketAddress(*st);
            },
            [&](const UnixSocket &s) -> absl::StatusOr<SocketAddress> {
              auto st = s.GetPeerName();
              if (!st.ok()) {
                return st;
              }
              return SocketAddress(*st);
            }},
        socket_);
  }

  absl::StatusOr<SocketAddress> LocalAddress(int port) const {
    return std::visit(
        EyeOfNewt{
            [&](const TCPSocket &s) -> absl::StatusOr<SocketAddress> {
              auto st = s.LocalAddress(port);
              if (!st.ok()) {
                return st;
              }
              return SocketAddress(*st);
            },
            [&](const VirtualStreamSocket &s) -> absl::StatusOr<SocketAddress> {
              auto st = s.LocalAddress(port);
              if (!st.ok()) {
                return st;
              }
              return SocketAddress(*st);
            },
            [&](const UnixSocket &s) -> absl::StatusOr<SocketAddress> {
              auto st = s.LocalAddress();
              if (!st.ok()) {
                return st;
              }
              return SocketAddress(*st);
            }},
        socket_);
  }

private:
  // Constructors with various socket types.
  StreamSocket(const TCPSocket &s) : socket_(s) {}
  StreamSocket(const VirtualStreamSocket &s) : socket_(s) {}
  StreamSocket(const UnixSocket &s) : socket_(s) {}

  std::variant<TCPSocket, VirtualStreamSocket, UnixSocket> socket_;
};

} // namespace toolbelt

#endif //  __TOOLBELT_SOCKETS_H
