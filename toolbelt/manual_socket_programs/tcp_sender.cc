#include "toolbelt/sockets.h"

int main(int argc, char *argv[]) {
  // TCP socket sender.
  // 2 args:
  // 1: ip address of receiver
  // 2: port of receiver
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <ip_address> <port>" << std::endl;
    return 1;
  }

  std::string ip_address = argv[1];
  int port = std::stoi(argv[2]);

  // Create a TCP socket and connect to the receiver.
  toolbelt::TCPSocket socket;
  toolbelt::InetAddress addr(ip_address, port);
  absl::Status status = socket.Connect(addr);
  if (!status.ok()) {
    std::cerr << "Failed to connect: " << status.message() << std::endl;
    return 1;
  }

  std::cout << "Connected to " << ip_address << ":" << port << std::endl;

  char buffer[1024];
  char *buf = buffer + sizeof(int32_t);
  size_t buflen = sizeof(buffer) - sizeof(int32_t);
  for (int i = 0; i < 10; ++i) {
    // Send a message to the receiver.  This is prefixed with the message
    // length.
    snprintf(buf, buflen, "Hello, receiver! Message number: %d", i);
    absl::StatusOr<ssize_t> n = socket.SendMessage(buf, strlen(buf));
    if (!n.ok()) {
      std::cerr << "Failed to send message: " << n.status().message()
                << std::endl;
      return 1;
    }
    std::cout << "Sent " << *n << " bytes: " << buf << std::endl;
  }

  return 0;
}