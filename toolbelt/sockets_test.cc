#include "sockets.h"
#include <arpa/inet.h>
#include <cstring>
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view TEST_DATA = "The quick brown fox jumped over the lazy dog.";
const static absl::Duration LOOPBACK_TIMEOUT = absl::Milliseconds(10);

// Test class to hold on to a randomly assigned unused port until destruction
// Any tests binding to this unused port will probably need to call NetworkSocket::SetReusePort
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

TEST(SocketsTest, UDPSocket_SendAndReceiveUnicast) {
  UnusedPort port;
  auto sender = toolbelt::UDPSocket();
  auto receiver = toolbelt::UDPSocket();

  ASSERT_TRUE(receiver.SetReusePort().ok());
  ASSERT_TRUE(receiver.Bind(toolbelt::InetAddress("localhost", port)).ok());

  toolbelt::InetAddress sendto_address("localhost", port);
  ASSERT_TRUE(sender.SendTo(sendto_address, TEST_DATA.data(), TEST_DATA.size()).ok());

  std::vector<char> receive_buffer(TEST_DATA.size());
  ASSERT_EQ(*receiver.Receive(receive_buffer.data(), receive_buffer.size()), TEST_DATA.size());
  ASSERT_EQ(std::string_view(receive_buffer.data(), receive_buffer.size()), TEST_DATA);

  ASSERT_EQ(0, std::strcmp(receive_buffer.data(), TEST_DATA.data()));
}

TEST(SocketsTest, UDPSocket_SendAndReceiveBroadcast) {
  UnusedPort port;
  auto sender = toolbelt::UDPSocket();
  auto receiver = toolbelt::UDPSocket();

  ASSERT_TRUE(sender.SetBroadcast().ok());

  ASSERT_TRUE(receiver.SetReusePort().ok());
  ASSERT_TRUE(receiver.Bind(toolbelt::InetAddress(toolbelt::InetAddress::AnyAddress(port))).ok());

  toolbelt::InetAddress sendto_address(toolbelt::InetAddress::BroadcastAddress(port));
  ASSERT_TRUE(sender.SendTo(sendto_address, TEST_DATA.data(), TEST_DATA.size()).ok());

  std::vector<char> receive_buffer(TEST_DATA.size());
  ASSERT_EQ(*receiver.Receive(receive_buffer.data(), receive_buffer.size()), TEST_DATA.size());
  ASSERT_EQ(std::string_view(receive_buffer.data(), receive_buffer.size()), TEST_DATA);

  ASSERT_EQ(0, std::strcmp(receive_buffer.data(), TEST_DATA.data()));
}

TEST(SocketsTest, UDPSocket_SendAndReceiveMulticast) {
    UnusedPort port;
    std::string multicast_ip = "224.0.0.205";
    toolbelt::InetAddress multicast_address(multicast_ip, port);

    auto sender = toolbelt::UDPSocket();
    auto receiver = toolbelt::UDPSocket();

    ASSERT_TRUE(sender.SetMulticastLoop().ok());

    std::vector<char> receive_buffer(TEST_DATA.size());
    ASSERT_TRUE(receiver.SetReusePort().ok());
    ASSERT_TRUE(receiver.SetNonBlocking().ok());
    ASSERT_TRUE(receiver.Bind(toolbelt::InetAddress::AnyAddress(port)).ok());

    ASSERT_TRUE(receiver.JoinMulticastGroup(multicast_address).ok());
    ASSERT_TRUE(sender.SendTo(multicast_address, TEST_DATA.data(), TEST_DATA.size()).ok());

    absl::Time timeout = absl::Now() + LOOPBACK_TIMEOUT;
    while (absl::Now() < timeout) {
        auto status_or_len = receiver.Receive(receive_buffer.data(), receive_buffer.size());
        if (status_or_len.ok()) {
            ASSERT_EQ(*status_or_len, TEST_DATA.size());
            ASSERT_EQ(std::string_view(receive_buffer.data(), receive_buffer.size()), TEST_DATA);
            break;
        }
    }

    ASSERT_TRUE(receiver.LeaveMulticastGroup(multicast_address).ok());
    ASSERT_TRUE(sender.SendTo(multicast_address, TEST_DATA.data(), TEST_DATA.size()).ok());
    timeout = absl::Now() + LOOPBACK_TIMEOUT;
    while (absl::Now() < timeout) {
        auto status_or_len = receiver.Receive(receive_buffer.data(), receive_buffer.size());
        if (status_or_len.ok()) {
            FAIL() << "Received " << *status_or_len << " bytes but expected nothing";
        }
    }
}
