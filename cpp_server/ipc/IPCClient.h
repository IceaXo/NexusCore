#pragma once
#include <string>

// IPC 通信客户端 —— C++ 服务器通过 TCP 连接本地 Python AI 进程
// 和 Connection 的定位一样：纯字节搬运工，不涉及任何业务逻辑
class IPCClient {
public:
    int ipc_fd;              // 连接 Python 的 Socket 句柄
    std::string recv_buffer; // 接收蓄水池 —— 和外网玩家的 Connection 一样需要它
    std::string send_buffer; // 发送池 —— 当内核缓冲区满时，数据暂存在这里

    IPCClient();
    ~IPCClient();

    // 主动拨号到 127.0.0.1:port (Python AI 监听的本地端口)
    // 连接成功后，必须把 ipc_fd 设为非阻塞，否则 Epoll 管不了它
    bool ConnectToAI(int port);

    // 把 Room 广播过来的战局 JSON 打包，非阻塞发给 Python
    // 如果一次发不完，剩余的留在 send_buffer 里，等 EPOLLOUT 再发
    bool SendSnapshot(const std::string& json_data);

    // 死循环 recv 直到 EAGAIN，数据追加到 recv_buffer
    // 和 Connection::ReadFromSocket 完全一致
    bool ReadFromSocket();

    // 从 send_buffer 头部 send，发多少删多少，遇到 EAGAIN 停下
    bool FlushSendBuffer();

    // 被 Epoll 叫醒时调用 —— 从 recv_buffer 里切出一条完整的 AI 决策
    // 切包逻辑和 Connection::ExtractMessage 完全一致：
    //   4 字节大端长度头 → 验证收全 → substr 切割 → erase 销毁
    std::string ExtractAIDecision();
};
