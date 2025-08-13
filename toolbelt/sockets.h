// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_SOCKETS_H
#define __TOOLBELT_SOCKETS_H
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "coroutine.h"
#include "fd.h"
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/vsock.h>
#include <unistd.h>
#include <variant>
#include <vector>

namespace toolbelt {

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
  struct sockaddr_in addr_; // In network byte order.
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

  // An address with VMADDR_CID_ANY and the give port number (host order)
  VirtualAddress(uint32_t port);

  VirtualAddress(uint32_t cid, uint32_t port);

  VirtualAddress(const struct sockaddr_vm &addr) : addr_(addr), valid_(true) {}

  const sockaddr_vm &GetAddress() const { return addr_; }
  socklen_t GetLength() const { return sizeof(addr_); }
  bool Valid() const { return valid_; }

  // CID and port are returned in host byte order.
  uint32_t Cid() const { return addr_.svm_cid; }
  uint32_t Port() const { return addr_.svm_port; }

  // Port is in host byte order.
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
  struct sockaddr_vm addr_; // In network byte order.
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
  SocketAddress(int index, const void *addr) {
    switch (index) {
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
    switch (addr.Type()) {
    case kAddressInet:
      return SocketAddress(addr.GetInetAddress().IpAddress(),
                           0); // Port is 0 for any port.
    case kAddressVirtual:
      return SocketAddress(addr.GetVirtualAddress().Cid(), VMADDR_PORT_ANY);
    case kAddressUnix:
      return SocketAddress(std::string(addr.GetUnixAddress()));

    default:
      throw std::invalid_argument("Invalid socket address type");
    }
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
    if (std::holds_alternative<InetAddress>(address_)) {
      return std::get<InetAddress>(address_).ToString();
    } else if (std::holds_alternative<VirtualAddress>(address_)) {
      return std::get<VirtualAddress>(address_).ToString();
    } else if (std::holds_alternative<std::string>(address_)) {
      return std::get<std::string>(address_);
    }
    return "Invalid Address";
  }

  bool Valid() const {
    switch (address_.index()) {
    case 0:
      return std::get<InetAddress>(address_).Valid();
    case 1:
      return std::get<VirtualAddress>(address_).Valid();
    case 2:
      return !std::get<std::string>(address_).empty();
    default:
      return false;
    }
  }

  // What address type is in the variant.
  int Type() const { return address_.index(); }

  int Port() const {
    switch (address_.index()) {
    case 0:
      return std::get<InetAddress>(address_).Port();
    case 1:
      return std::get<VirtualAddress>(address_).Port();
    case 2:
      return 0; // Unix domain sockets don't have a port.
    default:
      throw std::invalid_argument("Invalid socket address type");
    }
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
  switch (a.address_.index()) {
  case 0:
    return AbslHashValue(std::move(h), std::get<InetAddress>(a.address_));
  case 1:
    return AbslHashValue(std::move(h), std::get<VirtualAddress>(a.address_));
  case 2:
    return AbslHashValue(std::move(h),
                         std::string_view(std::get<std::string>(a.address_)));
  default:
    return std::move(h);
  }
}

// This is a general socket initialized with a file descriptor.  Subclasses
// implement the different socket types.
class Socket {
public:
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

  void Close() {
    fd_.Close();
    connected_ = false;
  }
  bool Connected() const { return fd_.Valid() && connected_; }

  // Send and receive raw buffers.
  absl::StatusOr<ssize_t> Receive(char *buffer, size_t buflen,
                                  co::Coroutine *c = nullptr);
  absl::StatusOr<ssize_t> Send(const char *buffer, size_t length,
                               co::Coroutine *c = nullptr);
  // Send and receive length-delimited message.  The length is a 4-byte
  // network byte order (big endian) int as the first 4 bytes and
  // contains the length of the message.
  absl::StatusOr<ssize_t> ReceiveMessage(char *buffer, size_t buflen,
                                         co::Coroutine *c = nullptr);

  absl::StatusOr<std::vector<char>>
  ReceiveVariableLengthMessage(co::Coroutine *c = nullptr);

  // For SendMessage, the buffer pointer must be 4 bytes beyond
  // the actual buffer start, which must be length+4 bytes
  // long.  We write exactly length+4 bytes to the socket starting
  // at buffer-4.  This is to allow us to do a single send
  // to the socket rather than splitting it into 2.
  absl::StatusOr<ssize_t> SendMessage(char *buffer, size_t length,
                                      co::Coroutine *c = nullptr);

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
  UnixSocket(UnixSocket &&s) : Socket(std::move(s)) {}
  UnixSocket(const UnixSocket &s) = default;
  UnixSocket &operator=(const UnixSocket &s) = default;
  UnixSocket &operator=(UnixSocket &&s) = default;

  ~UnixSocket() = default;

  absl::Status Bind(const std::string &pathname, bool listen);
  absl::Status Connect(const std::string &pathname);

  absl::StatusOr<UnixSocket> Accept(co::Coroutine *c = nullptr);

  absl::Status SendFds(const std::vector<FileDescriptor> &fds,
                       co::Coroutine *c = nullptr);
  absl::Status ReceiveFds(std::vector<FileDescriptor> &fds,
                          co::Coroutine *c = nullptr);

  std::string BoundAddress() const { return bound_address_; }

private:
  std::string bound_address_;
};

// A socket for communication across the network.  This is the base
// class for UDP and TCP sockets.
class NetworkSocket : public Socket {
public:
  NetworkSocket() = default;
  explicit NetworkSocket(int fd, bool connected = false)
      : Socket(fd, connected) {}
  NetworkSocket(const NetworkSocket &s)
      : Socket(s), bound_address_(s.bound_address_) {}
  NetworkSocket(NetworkSocket &&s)
      : Socket(std::move(s)), bound_address_(std::move(s.bound_address_)) {}
  ~NetworkSocket() = default;
  NetworkSocket &operator=(const NetworkSocket &s) = default;

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
  UDPSocket(const UDPSocket &) = default;
  UDPSocket(UDPSocket &&s) : NetworkSocket(std::move(s)) {}
  ~UDPSocket() = default;
  UDPSocket &operator=(const UDPSocket &s) = default;

  absl::Status Bind(const InetAddress &addr);

  absl::Status JoinMulticastGroup(const InetAddress &addr);
  absl::Status LeaveMulticastGroup(const InetAddress &addr);

  // NOTE: Read and Write may or may not work on UDP sockets.  Use SendTo and
  // Receive for datagrams.
  absl::Status SendTo(const InetAddress &addr, const void *buffer,
                      size_t length, co::Coroutine *c = nullptr);
  absl::StatusOr<ssize_t> Receive(void *buffer, size_t buflen,
                                  co::Coroutine *c = nullptr);
  absl::StatusOr<ssize_t> ReceiveFrom(InetAddress &sender, void *buffer,
                                      size_t buflen,
                                      co::Coroutine *c = nullptr);
  absl::Status SetBroadcast();
  absl::Status SetMulticastLoop();
};

// A TCP based socket.
class TCPSocket : public NetworkSocket {
public:
  TCPSocket();
  explicit TCPSocket(int fd, bool connected = false)
      : NetworkSocket(fd, connected) {}
  TCPSocket(const TCPSocket &) = default;
  TCPSocket(TCPSocket &&s) : NetworkSocket(std::move(s)) {}
  ~TCPSocket() = default;
  TCPSocket &operator=(const TCPSocket &s) = default;

  absl::Status Bind(const InetAddress &addr, bool listen);

  absl::StatusOr<TCPSocket> Accept(co::Coroutine *c = nullptr);
};

class VirtualStreamSocket : public Socket {
public:
  VirtualStreamSocket();
  explicit VirtualStreamSocket(int fd, bool connected = false)
      : Socket(fd, connected) {}
  VirtualStreamSocket(const VirtualStreamSocket &) = default;
  VirtualStreamSocket(VirtualStreamSocket &&s) : Socket(std::move(s)) {}
  ~VirtualStreamSocket() = default;
  VirtualStreamSocket &operator=(const VirtualStreamSocket &s) = default;
  absl::Status Connect(const VirtualAddress &addr);

  absl::Status Bind(const VirtualAddress &addr, bool listen);

  absl::StatusOr<VirtualStreamSocket> Accept(co::Coroutine *c = nullptr);
  absl::StatusOr<VirtualAddress> LocalAddress(uint32_t port) const;

  const VirtualAddress &BoundAddress() const { return bound_address_; }

protected:
  VirtualAddress bound_address_;
};

// Class that wraps the various stream-based sockets.
class StreamSocket {
public:
  StreamSocket() = default;
  StreamSocket(const StreamSocket &s) = default;
  StreamSocket(StreamSocket &&s) = default;
  ~StreamSocket() = default;
  StreamSocket &operator=(const StreamSocket &s) = default;

  // Binders for TCP, virtual, and Unix sockets.
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
      return std::get<TCPSocket>(socket_).Connect(addr.GetInetAddress());
    case SocketAddress::kAddressVirtual:
      return std::get<VirtualStreamSocket>(socket_).Connect(
          addr.GetVirtualAddress());
    case SocketAddress::kAddressUnix:
      return std::get<UnixSocket>(socket_).Connect(addr.GetUnixAddress());
    }
    return absl::Status(absl::StatusCode::kInternal, "Invalid socket address");
  }

  absl::StatusOr<StreamSocket> Accept(co::Coroutine *c = nullptr) {
    switch (socket_.index()) {
    case 0: {
      auto s = std::get<TCPSocket>(socket_).Accept(c);
      if (!s.ok()) {
        return s.status();
      }
      return StreamSocket(std::move(*s));
    }
    case 1: {
      auto s = std::get<VirtualStreamSocket>(socket_).Accept(c);
      if (!s.ok()) {
        return s.status();
      }
      return StreamSocket(std::move(*s));
    }
    case 2: {
      auto s = std::get<UnixSocket>(socket_).Accept(c);
      if (!s.ok()) {
        return s.status();
      }
      return StreamSocket(std::move(*s));
    }
    }
    return absl::Status(absl::StatusCode::kInternal, "Invalid socket type");
  }

  // Accessors for the underlying socket types.
  TCPSocket &GetTCPSocket() { return std::get<TCPSocket>(socket_); }
  VirtualStreamSocket &GetVirtualStreamSocket() {
    return std::get<VirtualStreamSocket>(socket_);
  }
  UnixSocket &GetUnixSocket() { return std::get<UnixSocket>(socket_); }

  SocketAddress BoundAddress() const {
    switch (socket_.index()) {
    case 0:
      return SocketAddress(std::get<TCPSocket>(socket_).BoundAddress());
    case 1:
      return SocketAddress(
          std::get<VirtualStreamSocket>(socket_).BoundAddress());
    case 2:
      return SocketAddress(std::get<UnixSocket>(socket_).BoundAddress());
    }
    return SocketAddress(); // Invalid address.
  }

  void Close() {
    switch (socket_.index()) {
    case 0:
      std::get<TCPSocket>(socket_).Close();
      break;
    case 1:
      std::get<VirtualStreamSocket>(socket_).Close();
      break;
    case 2:
      std::get<UnixSocket>(socket_).Close();
      break;
    }
  }
  bool Connected() const {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).Connected();
    case 1:
      return std::get<VirtualStreamSocket>(socket_).Connected();
    case 2:
      return std::get<UnixSocket>(socket_).Connected();
    }
    return false; // Invalid socket type.
  }

  // Send and receive raw buffers.
  absl::StatusOr<ssize_t> Receive(char *buffer, size_t buflen,
                                  co::Coroutine *c = nullptr) {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).Receive(buffer, buflen, c);
    case 1:
      return std::get<VirtualStreamSocket>(socket_).Receive(buffer, buflen, c);
    case 2:
      return std::get<UnixSocket>(socket_).Receive(buffer, buflen, c);
    }
    return absl::InternalError("Invalid socket type");
  }

  absl::StatusOr<ssize_t> Send(const char *buffer, size_t length,
                               co::Coroutine *c = nullptr) {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).Send(buffer, length, c);
    case 1:
      return std::get<VirtualStreamSocket>(socket_).Send(buffer, length, c);
    case 2:
      return std::get<UnixSocket>(socket_).Send(buffer, length, c);
    }
    return absl::InternalError("Invalid socket type");
  }
  // Send and receive length-delimited message.  The length is a 4-byte
  // network byte order (big endian) int as the first 4 bytes and
  // contains the length of the message.
  absl::StatusOr<ssize_t> ReceiveMessage(char *buffer, size_t buflen,
                                         co::Coroutine *c = nullptr) {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).ReceiveMessage(buffer, buflen, c);
    case 1:
      return std::get<VirtualStreamSocket>(socket_).ReceiveMessage(buffer,
                                                                   buflen, c);
    case 2:
      return std::get<UnixSocket>(socket_).ReceiveMessage(buffer, buflen, c);
    }
    return absl::InternalError("Invalid socket type");
  }

  absl::StatusOr<std::vector<char>>
  ReceiveVariableLengthMessage(co::Coroutine *c = nullptr) {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).ReceiveVariableLengthMessage(c);
    case 1:
      return std::get<VirtualStreamSocket>(socket_)
          .ReceiveVariableLengthMessage(c);
    case 2:
      return std::get<UnixSocket>(socket_).ReceiveVariableLengthMessage(c);
    }
    return absl::InternalError("Invalid socket type");
  }

  // For SendMessage, the buffer pointer must be 4 bytes beyond
  // the actual buffer start, which must be length+4 bytes
  // long.  We write exactly length+4 bytes to the socket starting
  // at buffer-4.  This is to allow us to do a single send
  // to the socket rather than splitting it into 2.
  absl::StatusOr<ssize_t> SendMessage(char *buffer, size_t length,
                                      co::Coroutine *c = nullptr) {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).SendMessage(buffer, length, c);
    case 1:
      return std::get<VirtualStreamSocket>(socket_).SendMessage(buffer, length,
                                                                c);
    case 2:
      return std::get<UnixSocket>(socket_).SendMessage(buffer, length, c);
    }
    return absl::InternalError("Invalid socket type");
  }

  absl::Status SetNonBlocking() {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).SetNonBlocking();
    case 1:
      return std::get<VirtualStreamSocket>(socket_).SetNonBlocking();
    case 2:
      return std::get<UnixSocket>(socket_).SetNonBlocking();
    }
    return absl::InternalError("Invalid socket type");
  }
  // Get the fd on which to poll for non-blocking operations.
  FileDescriptor GetFileDescriptor() const {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).GetFileDescriptor();
    case 1:
      return std::get<VirtualStreamSocket>(socket_).GetFileDescriptor();
    case 2:
      return std::get<UnixSocket>(socket_).GetFileDescriptor();
    }
    return FileDescriptor(); // Invalid socket type.
  }

  absl::Status SetCloseOnExec() {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).SetCloseOnExec();
    case 1:
      return std::get<VirtualStreamSocket>(socket_).SetCloseOnExec();
    case 2:
      return std::get<UnixSocket>(socket_).SetCloseOnExec();
    }
    return absl::InternalError("Invalid socket type");
  }

  bool IsNonBlocking() const {
    switch (socket_.index()) {
    case 0:
      return std::get<TCPSocket>(socket_).IsNonBlocking();
    case 1:
      return std::get<VirtualStreamSocket>(socket_).IsNonBlocking();
    case 2:
      return std::get<UnixSocket>(socket_).IsNonBlocking();
    }
    return false; // Invalid socket type.
  }
  bool IsBlocking() const { return !IsNonBlocking(); }

private:
  // Constructors with various socket types.
  StreamSocket(const TCPSocket &s) : socket_(s) {}
  StreamSocket(const VirtualStreamSocket &s) : socket_(s) {}
  StreamSocket(const UnixSocket &s) : socket_(s) {}

  std::variant<TCPSocket, VirtualStreamSocket, UnixSocket> socket_;
};

} // namespace toolbelt

#endif //  __TOOLBELT_SOCKETS_H
