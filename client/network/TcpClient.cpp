#include "TcpClient.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

TcpClient::TcpClient() {}

TcpClient::~TcpClient() { Disconnect(); }

bool TcpClient::Connect(const std::string& host, uint16_t port) {
    if (connected_) return true;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[TcpClient] WSAStartup failed" << std::endl;
        return false;
    }

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "[TcpClient] socket() failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    // Resolve hostname (supports both IP strings and domain names)
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port);

    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        std::cerr << "[TcpClient] getaddrinfo failed: " << WSAGetLastError() << std::endl;
        closesocket(sock_);
        sock_ = -1;
        WSACleanup();
        return false;
    }

    int ret = connect(sock_, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (ret != 0) {
        std::cerr << "[TcpClient] connect() failed: " << WSAGetLastError() << std::endl;
        closesocket(sock_);
        sock_ = -1;
        WSACleanup();
        return false;
    }

    std::cout << "[TcpClient] Connected to " << host << ":" << port << std::endl;

    running_ = true;
    connected_ = true;
    net_thread_ = std::thread(&TcpClient::NetworkLoop, this);
    return true;
}

bool TcpClient::Send(const std::string& json) {
    if (!connected_) return false;

    std::lock_guard<std::mutex> lock(send_mutex_);

    // 4-byte big-endian length prefix (matches server protocol exactly)
    uint32_t body_len = htonl(static_cast<uint32_t>(json.size()));
    if (send(sock_, reinterpret_cast<const char*>(&body_len), 4, 0) != 4)
        return false;
    if (send(sock_, json.data(), static_cast<int>(json.size()), 0) != static_cast<int>(json.size()))
        return false;
    return true;
}

void TcpClient::SetOnMessage(MessageCallback cb) {
    on_message_ = std::move(cb);
}

void TcpClient::Disconnect() {
    running_ = false;
    if (sock_ != -1) {
        closesocket(sock_);
        sock_ = -1;
    }
    if (net_thread_.joinable())
        net_thread_.join();
    WSACleanup();
    connected_ = false;
}

bool TcpClient::ReadExact(void* buf, size_t n) {
    // MSG_WAITALL ensures we read exactly n bytes or fail
    size_t remaining = n;
    char* ptr = static_cast<char*>(buf);
    while (remaining > 0) {
        int got = recv(sock_, ptr, static_cast<int>(remaining), 0);
        if (got <= 0) return false;
        remaining -= got;
        ptr += got;
    }
    return true;
}

void TcpClient::NetworkLoop() {
    while (running_) {
        // Read 4-byte big-endian length prefix
        uint32_t body_len;
        if (!ReadExact(&body_len, 4)) break;

        body_len = ntohl(body_len);
        if (body_len == 0 || body_len > MAX_PACKET_SIZE) {
            std::cerr << "[TcpClient] Bad packet length: " << body_len << std::endl;
            break;
        }

        // Read body
        std::string body(body_len, '\0');
        if (!ReadExact(&body[0], body_len)) break;

        // Deliver complete JSON message to callback
        if (on_message_)
            on_message_(body);
    }

    connected_ = false;
    std::cerr << "[TcpClient] Disconnected from server" << std::endl;
}
