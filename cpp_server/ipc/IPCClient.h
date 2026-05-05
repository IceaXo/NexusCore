#pragma once
#include <string>
#include <cstdint>

// IPC 通信客户端 —— C++ 服务器通过 TCP 连接本地 Python AI 进程
// 和 Connection 的定位一样：纯字节搬运工，不涉及任何业务逻辑
class IPCClient {
public:
    static constexpr uint32_t MAX_PACKET_SIZE = 64 * 1024; // 64KB 上限

    int ipc_fd;              // 连接 Python 的 Socket 句柄
    std::string recv_buffer; // 接收蓄水池
    std::string send_buffer; // 发送池 —— 当内核缓冲区满时，数据暂存在这里
    bool bad_packet = false; // 收到恶意超大包头时置 true

    IPCClient();
    ~IPCClient();

    // 主动拨号到 127.0.0.1:port (Python AI 监听的本地端口)
    bool ConnectToAI(int port);

    // 把战局 JSON 打包发送。若 send_buffer 非空（旧数据未发完），
    // 新帧必须 append 到队尾，绝不能直接 send 插队导致 TCP 乱序。
    bool SendSnapshot(const std::string& json_data);

    // 死循环 recv 直到 EAGAIN
    bool ReadFromSocket();

    // 循环 send 直到 EAGAIN 或 send_buffer 清空
    bool FlushSendBuffer();

    // 从 recv_buffer 里切出一条完整的 AI 决策
    std::string ExtractAIDecision();
};
