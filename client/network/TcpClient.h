#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

class TcpClient {
public:
    static constexpr uint32_t MAX_PACKET_SIZE = 64 * 1024;

    using MessageCallback = std::function<void(const std::string& json)>;

    TcpClient();
    ~TcpClient();

    bool Connect(const std::string& host, uint16_t port);
    bool Send(const std::string& json);
    void SetOnMessage(MessageCallback cb);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }

private:
    void NetworkLoop();
    // Align with server: 4-byte big-endian length prefix, ntohl/htonl
    bool ReadExact(void* buf, size_t n);
    int sock_ = -1;
    std::mutex send_mutex_;

    MessageCallback on_message_;

    std::thread net_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
};
