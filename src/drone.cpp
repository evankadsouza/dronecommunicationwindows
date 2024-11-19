#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network_utils.h"

#define PORT 8080
#define BUFFER_SIZE 1024
#define ACK_TIMEOUT 3   // Time in seconds to wait for ACK
#define MAX_RETRIES 5   // Maximum number of retries before marking failure
#define DISCOVERY_INTERVAL 10 // Interval in seconds for sending discovery packets

std::mutex queue_mutex;
std::condition_variable cv;

// Message structure
struct Message {
    std::string data;
    std::string target_ip;
    int retries = 0;
};

// Queue for outgoing messages
std::queue<Message> message_queue;

// Peer-to-peer message handler
std::unordered_map<std::string, bool> reachable_peers;

// Function to send messages with retry mechanism
void sender_thread(const std::string &drone_id) {
    SOCKET sender_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender_socket == INVALID_SOCKET) {
        std::cerr << "Sender: Failed to create socket: " << WSAGetLastError() << "\n";
        return;
    }

    sockaddr_in target_addr = {};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(PORT);

    while (true) {
        Message msg;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [] { return !message_queue.empty(); });

            msg = message_queue.front();
            message_queue.pop();
        }

        inet_pton(AF_INET, msg.target_ip.c_str(), &target_addr.sin_addr);
        std::string message_with_id = "Drone " + drone_id + ": " + msg.data;

        int send_result = sendto(sender_socket, message_with_id.c_str(), message_with_id.length(), 0,
                                 (sockaddr *)&target_addr, sizeof(target_addr));
        if (send_result == SOCKET_ERROR) {
            std::cerr << "Sender: Send failed: " << WSAGetLastError() << "\n";
            continue;
        }

        std::cout << "Sender: Sent \"" << message_with_id << "\" to " << msg.target_ip << "\n";

        // Wait for ACK
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sender_socket, &readfds);

        timeval timeout = {ACK_TIMEOUT, 0};
        if (select(0, &readfds, nullptr, nullptr, &timeout) > 0) {
            char buffer[BUFFER_SIZE];
            sockaddr_in ack_addr;
            int ack_len = sizeof(ack_addr);
            int recv_len = recvfrom(sender_socket, buffer, BUFFER_SIZE - 1, 0, (sockaddr *)&ack_addr, &ack_len);
            if (recv_len > 0) {
                buffer[recv_len] = '\0';
                if (std::string(buffer) == "ACK") {
                    std::cout << "Sender: ACK received from " << msg.target_ip << "\n";
                    continue;
                }
            }
        }

        // Retry if no ACK received
        msg.retries++;
        if (msg.retries < MAX_RETRIES) {
            std::cerr << "Sender: Retrying \"" << msg.data << "\" to " << msg.target_ip << "\n";
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                message_queue.push(msg);
            }
            cv.notify_one();
        } else {
            std::cerr << "Sender: Failed to deliver \"" << msg.data << "\" to " << msg.target_ip << "\n";
            reachable_peers[msg.target_ip] = false;
        }
    }

    closesocket(sender_socket);
}

// Function to listen for incoming messages and send ACKs
void listener_thread(const std::string &drone_id) {
    SOCKET listener_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (listener_socket == INVALID_SOCKET) {
        std::cerr << "Listener: Failed to create socket: " << WSAGetLastError() << "\n";
        return;
    }

    sockaddr_in listener_addr = {};
    listener_addr.sin_family = AF_INET;
    listener_addr.sin_port = htons(PORT);
    listener_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener_socket, (sockaddr *)&listener_addr, sizeof(listener_addr)) == SOCKET_ERROR) {
        std::cerr << "Listener: Bind failed: " << WSAGetLastError() << "\n";
        closesocket(listener_socket);
        return;
    }

    char buffer[BUFFER_SIZE];
    sockaddr_in sender_addr;
    socklen_t sender_addr_len = sizeof(sender_addr);

    while (true) {
        int recv_len = recvfrom(listener_socket, buffer, BUFFER_SIZE - 1, 0,
                                (sockaddr *)&sender_addr, &sender_addr_len);
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            std::string sender_ip = get_ip_string(sender_addr);

            if (std::string(buffer).find("DISCOVERY") != std::string::npos) {
                std::cout << "Listener (Drone " << drone_id << "): Received discovery packet from " << sender_ip << "\n";
                reachable_peers[sender_ip] = true;
            } else {
                std::cout << "Listener (Drone " << drone_id << "): Received \"" << buffer
                          << "\" from " << sender_ip << "\n";

                // Send ACK back to sender
                const char *ack_message = "ACK";
                sendto(listener_socket, ack_message, strlen(ack_message), 0, (sockaddr *)&sender_addr, sender_addr_len);
            }
        }
    }

    closesocket(listener_socket);
}

// Function to periodically broadcast discovery packets
void discovery_thread(const std::string &drone_id) {
    SOCKET discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket == INVALID_SOCKET) {
        std::cerr << "Discovery: Failed to create socket: " << WSAGetLastError() << "\n";
        return;
    }

    sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    const char *discovery_message = "DISCOVERY";
    int broadcast_opt = 1;
    setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, (char *)&broadcast_opt, sizeof(broadcast_opt));

    while (true) {
        int send_result = sendto(discovery_socket, discovery_message, strlen(discovery_message), 0,
                                 (sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        if (send_result == SOCKET_ERROR) {
            std::cerr << "Discovery: Send failed: " << WSAGetLastError() << "\n";
        } else {
            std::cout << "Discovery: Broadcast discovery packet from Drone " << drone_id << "\n";
        }
        std::this_thread::sleep_for(std::chrono::seconds(DISCOVERY_INTERVAL));
    }

    closesocket(discovery_socket);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <DroneID>\n";
        return 1;
    }

    initialize_winsock();

    std::string drone_id = argv[1];

    std::thread sender(sender_thread, drone_id);
    std::thread listener(listener_thread, drone_id);
    std::thread discovery(discovery_thread, drone_id);

    // Add example messages to the queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        message_queue.push({"Hello to Drone B", "192.168.0.2"});
    }
    cv.notify_one();

    sender.join();
    listener.join();
    discovery.join();

    cleanup_winsock();
    return 0;
}
