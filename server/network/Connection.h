#pragma once
#include <string>
#include <cstdint>
#include <ctime>

class Connection {
public:
    static constexpr uint32_t MAX_PACKET_SIZE = 64 * 1024; // 64KB 上限防 OOM
    static constexpr time_t HEARTBEAT_TIMEOUT = 5;         // 心跳超时 5 秒（对齐需求文档）

    int fd;
    std::string recv_buffer; // 蓄水池
    std::string send_buffer; // 发送池 —— 内核缓冲区满时暂存，等 EPOLLOUT 续发
    bool bad_packet = false; // 收到恶意超大包头时置 true，外层应踢掉连接
    time_t last_active_time; // 上次收到消息的时间戳

    Connection(int _fd) : fd(_fd), last_active_time(time(nullptr)) {}

    // 检查是否心跳超时
    bool IsHeartbeatTimeout() const {
        return time(nullptr) - last_active_time > HEARTBEAT_TIMEOUT;
    }

    // 从 recv_buffer 中切出一个完整的 JSON 字符串。
    // 若检测到 body_length > MAX_PACKET_SIZE，标记 bad_packet = true 并返回 ""。
    std::string ExtractMessage();

    // 死循环抽水直到 EAGAIN
    bool ReadFromSocket();

    // 循环 send 直到 EAGAIN 或 send_buffer 清空
    bool WriteToSocket();
};