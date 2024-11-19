#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

void initialize_winsock();
void cleanup_winsock();
std::string get_ip_string(const sockaddr_in &addr);

#endif
