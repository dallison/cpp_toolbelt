#include "toolbelt/sockets.h"

int main(int argc, char **argv) {
  // 2 args:
  // 1: receiver port
  // 2: protocol (tcp or vm)
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <port> <protocol>" << std::endl;
    return 1;
  }

  int port = std::stoi(argv[1]);
  std::string protocol = argv[2];

  // Create a TCP socket and bind it to the specified port.
  toolbelt::SocketAddress addr;
  if (protocol == "tcp") {
    addr = toolbelt::InetAddress::AnyAddress(port);
  } else if (protocol == "vm") {
    addr = toolbelt::VirtualAddress::AnyAddress(port);
  } else {
    std::cerr << "Unknown protocol: " << protocol << std::endl;
    return 1;
  }
  toolbelt::StreamSocket socket;
  absl::Status status = socket.Bind(addr, true);
  if (!status.ok()) {
    std::cerr << "Failed to bind socket: " << status.message() << std::endl;
    return 1;
  }

  // Print the local address.
  absl::StatusOr<toolbelt::SocketAddress> local_address =
      socket.LocalAddress(port);
  if (local_address.ok()) {
    std::cerr << "My local address is " << local_address->ToString()
              << std::endl;
  } else {
    std::cerr << "Failed to get local address: "
              << local_address.status().message() << std::endl;
  }

  std::cout << "Listening on port " << port << " with protocol " << protocol
            << std::endl;

  // Accept connection on socket.
  absl::StatusOr<toolbelt::StreamSocket> client_socket = socket.Accept();
  if (!client_socket.ok()) {
    std::cerr << "Failed to accept connection: " << client_socket.status()
              << std::endl;
    return 1;
  }

  absl::StatusOr<toolbelt::SocketAddress> peer_address =
      client_socket->GetPeerName();
  std::cerr << "Accepted connection from: " << peer_address->ToString()
            << std::endl;

  // Receive 10 messages from sender.
  for (int i = 0; i < 10; ++i) {
    // Receive a message from the sender.
    char message[1024];
    absl::StatusOr<ssize_t> status_or =
        client_socket->ReceiveMessage(message, sizeof(message));
    if (!status_or.ok()) {
      std::cerr << "Failed to receive message: " << status_or.status()
                << std::endl;
      return 1;
    }
    std::cerr << "Received " << *status_or
              << " bytes: " << std::string(message, *status_or) << std::endl;
  }

  return 0;
}