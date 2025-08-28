#include "toolbelt/sockets.h"

int main(int argc, char *argv[]) {
  // TCP socket sender.
  // 3 args:
  // 1: address of receiver (IP address or CID)
  // 2: port of receiver
  // 3: either "tcp" or "vm"
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " <address> <port> [tcp|vm]"
              << std::endl;
    return 1;
  }

  std::string address = argv[1];
  int port = std::stoi(argv[2]);
  std::string protocol = argv[3];

  toolbelt::SocketAddress addr;
  if (protocol == "tcp") {
    addr = toolbelt::InetAddress(address, port);
  } else if (protocol == "vm") {
    addr = toolbelt::VirtualAddress(std::atoi(address.c_str()), port);
  } else {
    std::cerr << "Unknown protocol: " << protocol << std::endl;
    return 1;
  }
  // Create a TCP socket and connect to the receiver.
  toolbelt::StreamSocket socket;
  absl::Status status = socket.Connect(addr);
  if (!status.ok()) {
    std::cerr << "Failed to connect: " << status.message() << std::endl;
    return 1;
  }

  std::cout << "Connected to " << address << ":" << port << std::endl;

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