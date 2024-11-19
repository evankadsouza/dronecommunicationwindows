//
// Created by DELL on 16-11-2024.
//
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "network_utils.h"

int main() {
    initialize_winsock();

    SOCKET sender_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket: " << WSAGetLastError() << "\n";
        cleanup_winsock();
        return 1;
    }

    sockaddr_in receiver_addr = {};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &receiver_addr.sin_addr);

    const char* message = "Hello from sender!";
    int send_result = sendto(sender_socket, message, strlen(message), 0,
                             (sockaddr*)&receiver_addr, sizeof(receiver_addr));
    if (send_result == SOCKET_ERROR) {
        std::cerr << "Send failed: " << WSAGetLastError() << "\n";
    } else {
        std::cout << "Message sent successfully.\n";
    }

    closesocket(sender_socket);
    cleanup_winsock();
    return 0;
}
