#include "sockets.h"
#include <arpa/inet.h>
#include <cstring>
#include <gtest/gtest.h>
#include <string_view>
#include <vector>
#include <thread>
#include "absl/status/status_matchers.h"
#include "toolbelt/hexdump.h"

#define VAR(a) a##__COUNTER__
#define EVAL_AND_ASSERT_OK(expr) EVAL_AND_ASSERT_OK2(VAR(r_), expr)

#define EVAL_AND_ASSERT_OK2(result, expr)                                      \
  ({                                                                           \
    auto result = (expr);                                                      \
    if (!result.ok()) {                                                        \
      std::cerr << result.status() << std::endl;                               \
    }                                                                          \
    ASSERT_OK(result);                                                         \
    std::move(*result);                                                        \
  })

#define ASSERT_OK(e) ASSERT_THAT(e, ::absl_testing::IsOk())

namespace {
constexpr std::string_view TEST_DATA = "The quick brown fox jumped over the lazy dog.";
const static absl::Duration LOOPBACK_TIMEOUT = absl::Milliseconds(10);

// Test class to hold on to a randomly assigned unused port until destruction
// Any tests Binding to this unused port will probably need to call NetworkSocket::SetReusePort
class UnusedPort {
public:
  UnusedPort() {
    static_cast<void>(socket.SetReusePort());
    static_cast<void>(socket.Bind(toolbelt::InetAddress::AnyAddress(0)));
  }
  ~UnusedPort() = default;
  operator int() {
      return socket.BoundAddress().Port();
  }

private:
  UnusedPort(const UnusedPort& copy) = delete;
  UnusedPort(UnusedPort&& move) = delete;
  void operator=(const UnusedPort& copy) = delete;
  void operator=(UnusedPort&& move) = delete;
  toolbelt::UDPSocket socket;
};
}

TEST(SocketsTest, InetAddresses) {
    toolbelt::InetAddress addr1;
    ASSERT_FALSE(addr1.Valid());

    toolbelt::InetAddress addr2 = toolbelt::InetAddress::BroadcastAddress(1234);
    ASSERT_TRUE(addr2.Valid());
    ASSERT_EQ(1234, addr2.Port());

    toolbelt::InetAddress addr3 = toolbelt::InetAddress::AnyAddress(4321);
    ASSERT_EQ(4321, addr3.Port());

    toolbelt::InetAddress local_ip = toolbelt::InetAddress("127.0.0.1", 1111);
    ASSERT_EQ(1111, local_ip.Port());
    ASSERT_EQ("127.0.0.1:1111", local_ip.ToString());

    toolbelt::InetAddress local_host = toolbelt::InetAddress("localhost", 2222);
    ASSERT_EQ(2222, local_host.Port());
    ASSERT_EQ("127.0.0.1:2222", local_host.ToString());

    toolbelt::InetAddress bad = toolbelt::InetAddress("foobardoesntexist", 2222);
    ASSERT_FALSE(bad.Valid());

    in_addr ipaddr;
    ASSERT_EQ(1, inet_pton(AF_INET, "127.0.0.1", &ipaddr.s_addr));
    toolbelt::InetAddress local_in = toolbelt::InetAddress(ipaddr, 3333);
    ASSERT_EQ(3333, local_in.Port());
    ASSERT_EQ("127.0.0.1:3333", local_in.ToString());
}

TEST(SocketsTest, UnixSocket) {
    char tmp[] = "/tmp/socketsXXXXXX";
    int fd = mkstemp(tmp);
    ASSERT_NE(-1, fd);
    std::string socket_name = tmp;
    close(fd);

    unlink(socket_name.c_str());
    co::CoroutineScheduler scheduler;

    toolbelt::UnixSocket listener;
    absl::Status status = listener.Bind(socket_name, true);
    std::cerr << status << std::endl;
    ASSERT_TRUE(status.ok());

    co::Coroutine incoming(scheduler, [&listener](co::Coroutine* c) {
        absl::StatusOr<toolbelt::UnixSocket> s = listener.Accept(c);
        ASSERT_TRUE(s.ok());
        auto socket = s.value();

        char buffer[256];
        // ReceiveMessage uses the 4 bytes below the buffer for the length.
        absl::StatusOr<ssize_t> nbytes = socket.ReceiveMessage(buffer + 4, sizeof(buffer) - 4, c);
        ASSERT_TRUE(nbytes.ok());
        auto n = nbytes.value();
        ASSERT_EQ(12, n);  // "hello world\0"
        ASSERT_EQ("hello world", std::string(buffer + 4, n - 1));
        std::vector<toolbelt::FileDescriptor> fds;

        absl::Status s2 = socket.ReceiveFds(fds, c);
        ASSERT_TRUE(s2.ok());
        ASSERT_EQ(3, fds.size());
    });

    co::Coroutine outgoing(scheduler, [&socket_name](co::Coroutine* c) {
        toolbelt::UnixSocket socket;
        absl::Status s = socket.Connect(socket_name);
        ASSERT_TRUE(s.ok());
        char buffer[256];
        // SendMessage uses the 4 bytes below the buffer for the length of the message.
        ssize_t n = snprintf(buffer + 4, sizeof(buffer) - 4, "hello world");
        n += 1;  // Include NUL at end.
        absl::StatusOr<ssize_t> nsent = socket.SendMessage(buffer + 4, n, c);
        ASSERT_TRUE(nsent.ok());
        ASSERT_EQ(n + 4, nsent.value());

        std::vector<toolbelt::FileDescriptor> fds;
        for (int i = 0; i < 3; i++) {
            // We dup the file descriptors to avoid closing stdout.
            fds.push_back(toolbelt::FileDescriptor(dup(i)));
        }
        absl::Status s2 = socket.SendFds(fds, c);
        ASSERT_TRUE(s2.ok());
    });

    scheduler.Run();
    remove(socket_name.c_str());
}

TEST(SocketsTest, UnixSocketErrors) {
    toolbelt::UnixSocket socket;
    // Socket is inValid, all will fail.
    ASSERT_FALSE(socket.Accept().ok());
    ASSERT_FALSE(socket.Connect("foobar").ok());
    std::vector<toolbelt::FileDescriptor> fds;
    ASSERT_FALSE(socket.SendFds(fds).ok());
    ASSERT_FALSE(socket.ReceiveFds(fds).ok());
}

TEST(SocketsTest, TCPSocket) {
    toolbelt::InetAddress addr("127.0.0.1", 6502);

    co::CoroutineScheduler scheduler;

    toolbelt::TCPSocket listener;
    ASSERT_TRUE(listener.SetReuseAddr().ok());
    absl::Status status = listener.Bind(addr, true);
    ASSERT_TRUE(status.ok());

    co::Coroutine incoming(scheduler, [&listener](co::Coroutine* c) {
        absl::StatusOr<toolbelt::TCPSocket> s = listener.Accept(c);
        ASSERT_TRUE(s.ok());
        auto socket = s.value();

        absl::StatusOr<std::vector<char>> b = socket.ReceiveVariableLengthMessage(c);
        ASSERT_TRUE(b.ok());
        auto buf = b.value();
        ASSERT_EQ(12, buf.size());  // "hello world\0"
        ASSERT_EQ("hello world", std::string(buf.data(), 11));
    });

    co::Coroutine outgoing(scheduler, [&addr](co::Coroutine* c) {
        toolbelt::TCPSocket socket;
        absl::Status s = socket.Connect(addr);
        ASSERT_TRUE(s.ok());
        char buffer[256];
        // SendMessage uses the 4 bytes below the buffer for the length of the message.
        ssize_t n = snprintf(buffer + 4, sizeof(buffer) - 4, "hello world");
        n += 1;  // Include NUL at end.
        absl::StatusOr<ssize_t> nsent = socket.SendMessage(buffer + 4, n, c);
        ASSERT_TRUE(nsent.ok());
        ASSERT_EQ(n + 4, nsent.value());
    });

    scheduler.Run();
}

TEST(SocketsTest, BigTCPSocketNonblocking) {
    toolbelt::InetAddress addr("127.0.0.1", 6502);

    co::CoroutineScheduler scheduler;

    toolbelt::TCPSocket listener;
    ASSERT_TRUE(listener.SetReuseAddr().ok());
    absl::Status status = listener.Bind(addr, true);
    ASSERT_TRUE(status.ok());

    constexpr size_t kBufferSize = 10 * 1024 * 1024;
    co::Coroutine incoming(scheduler, [&listener, kBufferSize](co::Coroutine* c) {
        absl::StatusOr<toolbelt::TCPSocket> s = listener.Accept(c);
        ASSERT_TRUE(s.ok());
        auto socket = s.value();
        ASSERT_OK(socket.SetNonBlocking());

        absl::StatusOr<std::vector<char>> b = socket.ReceiveVariableLengthMessage(c);
        ASSERT_TRUE(b.ok());
        auto buf = b.value();
        ASSERT_EQ(kBufferSize, buf.size());
        for (size_t i = 0; i < kBufferSize; i++) {
            if (buf[i] != 'a' + ((i + 4) % 26)) {
                std::cerr << "Mismatch at " << i << ": " << buf[i] << " != " << 'a' + (i % 26)
                          << "\n";
            }
            ASSERT_EQ('a' + ((i + 4) % 26), buf[i]);
        }
    });

    co::Coroutine outgoing(scheduler, [&addr](co::Coroutine* c) {
        toolbelt::TCPSocket socket;
        absl::Status s = socket.Connect(addr);
        ASSERT_TRUE(s.ok());
        ASSERT_OK(socket.SetNonBlocking());
        std::vector<char> buffer(kBufferSize + 4);
        for (size_t i = 4; i < buffer.size(); i++) {
            buffer[i] = 'a' + (i % 26);
        }
        absl::StatusOr<ssize_t> nsent =
                socket.SendMessage(buffer.data() + 4, buffer.size() - 4, c);
        ASSERT_TRUE(nsent.ok());
        ASSERT_EQ(buffer.size(), nsent.value());
    });

    scheduler.Run();
}

TEST(SocketsTest, BigTCPSocketBlocking) {
    toolbelt::InetAddress addr("127.0.0.1", 6502);

    co::CoroutineScheduler sendScheduler, ReceiveScheduler;

    toolbelt::TCPSocket listener;
    ASSERT_TRUE(listener.SetReuseAddr().ok());
    absl::Status status = listener.Bind(addr, true);
    ASSERT_TRUE(status.ok());

    constexpr size_t kBufferSize = 10 * 1024 * 1024;
    co::Coroutine incoming(
            sendScheduler, [&listener, kBufferSize](co::Coroutine* c) {
                absl::StatusOr<toolbelt::TCPSocket> s = listener.Accept(c);
                ASSERT_TRUE(s.ok());
                auto socket = s.value();

                absl::StatusOr<std::vector<char>> b = socket.ReceiveVariableLengthMessage(c);
                ASSERT_TRUE(b.ok());
                auto buf = b.value();
                ASSERT_EQ(kBufferSize, buf.size());
                for (size_t i = 0; i < kBufferSize; i++) {
                    if (buf[i] != 'a' + ((i + 4) % 26)) {
                        std::cerr << "Mismatch at " << i << ": " << buf[i]
                                  << " != " << 'a' + (i % 26) << "\n";
                    }
                    ASSERT_EQ('a' + ((i + 4) % 26), buf[i]);
                }
            });

    co::Coroutine outgoing(ReceiveScheduler, [&addr](co::Coroutine* c) {
        toolbelt::TCPSocket socket;
        absl::Status s = socket.Connect(addr);
        ASSERT_TRUE(s.ok());
        std::vector<char> buffer(kBufferSize + 4);
        for (size_t i = 4; i < buffer.size(); i++) {
            buffer[i] = 'a' + (i % 26);
        }
        absl::StatusOr<ssize_t> nsent =
                socket.SendMessage(buffer.data() + 4, buffer.size() - 4, c);
        ASSERT_TRUE(nsent.ok());
        ASSERT_EQ(buffer.size(), nsent.value());
    });

    std::thread sender([&sendScheduler]() { sendScheduler.Run(); });
    std::thread Receiver([&ReceiveScheduler]() { ReceiveScheduler.Run(); });
    sender.join();
    Receiver.join();
}

#if 0
TEST(SocketsTest, TCPSocketInterrupt) {
    // TODO(dave.allison): is there a way to pick an unused port?
    toolbelt::InetAddress addr("127.0.0.1", 6502);

    co::CoroutineScheduler scheduler;

    toolbelt::TCPSocket listener;
    ASSERT_TRUE(listener.SetReuseAddr().ok());
    absl::Status status = listener.Bind(addr, true);
    ASSERT_TRUE(status.ok());

    co::Coroutine incoming(
            scheduler,
            [&listener](co::Coroutine* c) {
                absl::StatusOr<toolbelt::TCPSocket> s = listener.Accept(c);
                ASSERT_FALSE(s.ok());
            },
            {.interrrupt_fd = scheduler.GetInterruptFd()});

    co::Coroutine interrupt(scheduler, [](co::Coroutine* c) {
        c->Yield();
        c->Scheduler().TriggerInterrupt();
    });

    scheduler.Run();
}
#endif

TEST(SocketsTest, TCPSocket2) {
    // TODO(dave.allison): is there a way to pick an unused port?
    toolbelt::InetAddress addr("127.0.0.1", 6502);

    co::CoroutineScheduler scheduler;

    toolbelt::TCPSocket listener;
    ASSERT_TRUE(listener.SetReuseAddr().ok());
    absl::Status status = listener.Bind(addr, true);
    ASSERT_TRUE(status.ok());

    co::Coroutine incoming(scheduler, [&listener](co::Coroutine* c) {
        absl::StatusOr<toolbelt::TCPSocket> s = listener.Accept(c);
        ASSERT_TRUE(s.ok());
        auto socket = s.value();

        char buffer[256];
        absl::StatusOr<ssize_t> nbytes = socket.Receive(buffer, 12, c);
        ASSERT_TRUE(nbytes.ok());
        auto n = nbytes.value();
        ASSERT_EQ(12, n);  // "hello world\0"
        ASSERT_EQ("hello world", std::string(buffer, n - 1));
        std::vector<toolbelt::FileDescriptor> fds;
    });

    co::Coroutine outgoing(scheduler, [&addr](co::Coroutine* c) {
        toolbelt::TCPSocket socket;
        absl::Status s = socket.Connect(addr);
        ASSERT_TRUE(s.ok());
        char buffer[256];
        ssize_t n = snprintf(buffer, sizeof(buffer), "hello world");
        n += 1;  // Include NUL at end.
        absl::StatusOr<ssize_t> nsent = socket.Send(buffer, n, c);
        ASSERT_TRUE(nsent.ok());
        ASSERT_EQ(n, nsent.value());
    });

    scheduler.Run();
}

TEST(SocketsTest, TCPSocket3) {
    toolbelt::InetAddress addr("127.0.0.1", 0);

    co::CoroutineScheduler scheduler;

    toolbelt::TCPSocket listener;
    ASSERT_TRUE(listener.SetReuseAddr().ok());
    ASSERT_TRUE(listener.SetReusePort().ok());
    absl::Status status = listener.Bind(addr, true);
    ASSERT_TRUE(status.ok());
    toolbelt::InetAddress baddr = listener.BoundAddress();

    co::Coroutine incoming(scheduler, [&listener](co::Coroutine* c) {
        absl::StatusOr<toolbelt::TCPSocket> s = listener.Accept(c);
        ASSERT_TRUE(s.ok());
        auto socket = s.value();

        char buffer[256];
        absl::StatusOr<ssize_t> nbytes = socket.Receive(buffer, 12, c);
        ASSERT_TRUE(nbytes.ok());
        auto n = nbytes.value();
        ASSERT_EQ(12, n);  // "hello world\0"
        ASSERT_EQ("hello world", std::string(buffer, n - 1));
        std::vector<toolbelt::FileDescriptor> fds;
    });

    co::Coroutine outgoing(scheduler, [&baddr](co::Coroutine* c) {
        toolbelt::TCPSocket socket;
        absl::Status s = socket.Connect(baddr);
        ASSERT_TRUE(s.ok());
        char buffer[256];
        ssize_t n = snprintf(buffer, sizeof(buffer), "hello world");
        n += 1;  // Include NUL at end.
        absl::StatusOr<ssize_t> nsent = socket.Send(buffer, n, c);
        ASSERT_TRUE(nsent.ok());
        ASSERT_EQ(n, nsent.value());
    });

    scheduler.Run();
}

TEST(SocketsTest, TCPSocketErrors) {
    toolbelt::TCPSocket socket;
    char buffer[256];

    // Socket is not Connected.  These will fail.
    toolbelt::InetAddress goodAddr("localhost", 2222);
    ASSERT_FALSE(socket.Connect(goodAddr).ok());  // Valid fd, but nothing is on this port.
    ASSERT_FALSE(socket.Send(buffer, 1).ok());
    ASSERT_FALSE(socket.Receive(buffer, 1).ok());
    ASSERT_FALSE(socket.SendMessage(buffer, 1).ok());
    ASSERT_FALSE(socket.ReceiveMessage(buffer, 1).ok());

    toolbelt::InetAddress badAddr =
            toolbelt::InetAddress("foobardoesntexist", 2222);
    ASSERT_FALSE(badAddr.Valid());
    ASSERT_FALSE(socket.Connect(badAddr).ok());

    socket.Close();
    ASSERT_FALSE(socket.Connect(goodAddr).ok());  // InValid fd.
}

TEST(SocketsTest, UDPSocket) {
    // TODO(dave.allison): is there a way to pick an unused port?
    toolbelt::InetAddress sender("127.0.0.1", 6502);
    toolbelt::InetAddress Receiver("127.0.0.1", 6503);

    co::CoroutineScheduler scheduler;

    co::Coroutine incoming(scheduler, [&Receiver](co::Coroutine* c) {
        toolbelt::UDPSocket socket;
        absl::Status s1 = socket.Bind(Receiver);
        ASSERT_TRUE(s1.ok());

        char buffer[256];
        absl::StatusOr<ssize_t> nbytes = socket.Receive(buffer, sizeof(buffer), c);
        ASSERT_TRUE(nbytes.ok());
        auto n = nbytes.value();
        ASSERT_EQ(12, n);  // "hello world\0"
        ASSERT_EQ("hello world", std::string(buffer, n - 1));
    });

    co::Coroutine outgoing(scheduler, [&sender, &Receiver](co::Coroutine* c) {
        toolbelt::UDPSocket socket;
        absl::Status s1 = socket.Bind(sender);
        ASSERT_TRUE(s1.ok());

        char buffer[256];
        ssize_t n = snprintf(buffer, sizeof(buffer), "hello world");
        n += 1;  // Include NUL at end.

        absl::Status s2 = socket.SendTo(Receiver, buffer, n, c);
        ASSERT_TRUE(s2.ok());
    });

    scheduler.Run();
}

TEST(SocketsTest, UDPSocket2) {
    // TODO(dave.allison): is there a way to pick an unused port?
    toolbelt::InetAddress sender("127.0.0.1", 6502);
    toolbelt::InetAddress receiver("127.0.0.1", 6503);

    co::CoroutineScheduler scheduler;

    co::Coroutine incoming(scheduler, [&receiver, &sender](co::Coroutine* c) {
        toolbelt::UDPSocket socket;
        absl::Status s1 = socket.Bind(receiver);
        ASSERT_TRUE(s1.ok());

        char buffer[256];
        toolbelt::InetAddress from;
        absl::StatusOr<ssize_t> nbytes = socket.ReceiveFrom(from, buffer, sizeof(buffer), c);
        ASSERT_TRUE(nbytes.ok());
        auto n = nbytes.value();
        ASSERT_EQ(12, n);  // "hello world\0"
        ASSERT_EQ("hello world", std::string(buffer, n - 1));
        ASSERT_EQ(sender, from);
    });

    co::Coroutine outgoing(scheduler, [&sender, &receiver](co::Coroutine* c) {
        toolbelt::UDPSocket socket;
        absl::Status s1 = socket.Bind(sender);
        ASSERT_TRUE(s1.ok());

        char buffer[256];
        ssize_t n = snprintf(buffer, sizeof(buffer), "hello world");
        n += 1;  // Include NUL at end.

        absl::Status s2 = socket.SendTo(receiver, buffer, n, c);
        ASSERT_TRUE(s2.ok());
    });

    scheduler.Run();
}

TEST(SocketsTest, UDPSocketBroadcast) {
    toolbelt::UDPSocket socket;
    ASSERT_TRUE(socket.SetBroadcast().ok());
}

TEST(SocketsTest, InValidAsString) {
    toolbelt::InetAddress addr;
    ASSERT_FALSE(addr.Valid());
    EXPECT_EQ(addr.ToString(), "0.0.0.0:0");
}


TEST(SocketsTest, UDPSocket_SendAndReceiveUnicast) {
  UnusedPort port;
  auto sender = toolbelt::UDPSocket();
  auto Receiver = toolbelt::UDPSocket();

  ASSERT_TRUE(Receiver.SetReusePort().ok());
  ASSERT_TRUE(Receiver.Bind(toolbelt::InetAddress("localhost", port)).ok());

  toolbelt::InetAddress sendto_address("localhost", port);
  ASSERT_TRUE(sender.SendTo(sendto_address, TEST_DATA.data(), TEST_DATA.size()).ok());

  std::vector<char> Receive_buffer(TEST_DATA.size());
  ASSERT_EQ(*Receiver.Receive(Receive_buffer.data(), Receive_buffer.size()), TEST_DATA.size());
  ASSERT_EQ(std::string_view(Receive_buffer.data(), Receive_buffer.size()), TEST_DATA);

  ASSERT_EQ(0, std::strcmp(Receive_buffer.data(), TEST_DATA.data()));
}

TEST(SocketsTest, UDPSocket_SendAndReceiveBroadcast) {
  UnusedPort port;
  auto sender = toolbelt::UDPSocket();
  auto Receiver = toolbelt::UDPSocket();

  ASSERT_TRUE(sender.SetBroadcast().ok());

  ASSERT_TRUE(Receiver.SetReusePort().ok());
  ASSERT_TRUE(Receiver.Bind(toolbelt::InetAddress(toolbelt::InetAddress::AnyAddress(port))).ok());

  toolbelt::InetAddress sendto_address(toolbelt::InetAddress::BroadcastAddress(port));
  ASSERT_TRUE(sender.SendTo(sendto_address, TEST_DATA.data(), TEST_DATA.size()).ok());

  std::vector<char> Receive_buffer(TEST_DATA.size());
  ASSERT_EQ(*Receiver.Receive(Receive_buffer.data(), Receive_buffer.size()), TEST_DATA.size());
  ASSERT_EQ(std::string_view(Receive_buffer.data(), Receive_buffer.size()), TEST_DATA);

  ASSERT_EQ(0, std::strcmp(Receive_buffer.data(), TEST_DATA.data()));
}

TEST(SocketsTest, UDPSocket_SendAndReceiveMulticast) {
    UnusedPort port;
    std::string multicast_ip = "224.0.0.205";
    toolbelt::InetAddress multicast_address(multicast_ip, port);

    auto sender = toolbelt::UDPSocket();
    auto Receiver = toolbelt::UDPSocket();

    ASSERT_TRUE(sender.SetMulticastLoop().ok());

    std::vector<char> Receive_buffer(TEST_DATA.size());
    ASSERT_TRUE(Receiver.SetReusePort().ok());
    ASSERT_TRUE(Receiver.SetNonBlocking().ok());
    ASSERT_TRUE(Receiver.Bind(toolbelt::InetAddress::AnyAddress(port)).ok());

    ASSERT_TRUE(Receiver.JoinMulticastGroup(multicast_address).ok());
    ASSERT_TRUE(sender.SendTo(multicast_address, TEST_DATA.data(), TEST_DATA.size()).ok());

    absl::Time timeout = absl::Now() + LOOPBACK_TIMEOUT;
    while (absl::Now() < timeout) {
        auto status_or_len = Receiver.Receive(Receive_buffer.data(), Receive_buffer.size());
        if (status_or_len.ok()) {
            ASSERT_EQ(*status_or_len, TEST_DATA.size());
            ASSERT_EQ(std::string_view(Receive_buffer.data(), Receive_buffer.size()), TEST_DATA);
            break;
        }
    }

    ASSERT_TRUE(Receiver.LeaveMulticastGroup(multicast_address).ok());
    ASSERT_TRUE(sender.SendTo(multicast_address, TEST_DATA.data(), TEST_DATA.size()).ok());
    timeout = absl::Now() + LOOPBACK_TIMEOUT;
    while (absl::Now() < timeout) {
        auto status_or_len = Receiver.Receive(Receive_buffer.data(), Receive_buffer.size());
        if (status_or_len.ok()) {
            FAIL() << "Received " << *status_or_len << " bytes but expected nothing";
        }
    }
}
