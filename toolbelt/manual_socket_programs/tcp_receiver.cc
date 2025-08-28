#include "toolbelt/sockets.h"

int main(int argc, char **argv) {
  // 1 arg: receiver port
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
    return 1;
  }

  int port = std::stoi(argv[1]);

  // Create a TCP socket and bind it to the specified port.
  toolbelt::TCPSocket socket;
  toolbelt::InetAddress addr = toolbelt::InetAddress::AnyAddress(port);
  absl::Status status = socket.Bind(addr, true);
  if (!status.ok()) {
    std::cerr << "Failed to bind socket: " << status.message() << std::endl;
    return 1;
  }

  std::cout << "Listening on port " << port << std::endl;

  // Accept connection on socket.
  absl::StatusOr<toolbelt::TCPSocket> client_socket = socket.Accept();
  if (!client_socket.ok()) {
    std::cerr << "Failed to accept connection: " << client_socket.status()
              << std::endl;
    return 1;
  }

  absl::StatusOr<toolbelt::InetAddress> peer_address =
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