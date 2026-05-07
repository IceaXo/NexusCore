#include "IPCClient.h"

#include <iostream>
#include <cstring>

#include <sys/socket.h>   // socket, connect, send, recv
#include <netinet/in.h>   // sockaddr_in, htons
#include <arpa/inet.h>    // inet_pton —— IP 字符串转二进制
#include <unistd.h>       // close
#include <fcntl.h>        // fcntl —— 操控文件描述符属性

IPCClient::IPCClient() : ipc_fd(-1) {}
//                        └─ 构造时先把 ipc_fd 标为 -1，代表"还没连接"

IPCClient::~IPCClient() {
    if (ipc_fd != -1) close(ipc_fd);
    //   如果连上了，主动挂断
}

// ===================================================================
//  主动拨号连接 Python AI 进程
//  流程：socket → connect → fcntl(设非阻塞)
//  和 nc 127.0.0.1 8081 做了一模一样的事，只是多了一步设为非阻塞
// ===================================================================
bool IPCClient::ConnectToAI(int port) {
    // ── 步骤 1：买电话机 ──
    // AF_INET      = IPv4
    // SOCK_STREAM  = TCP
    // 0            = 自动选协议 (TCP 对应 IPPROTO_TCP)
    ipc_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ipc_fd == -1) {
        std::cerr << "[IPC] 创建 Socket 失败" << std::endl;
        return false;
    }

    // ── 步骤 2：填写对方地址 (127.0.0.1:port) ──
    // sockaddr_in{} 的 {} 很关键：把所有没填的字段全部归零
    // 如果忘了 {}，sin_zero[8] 里残留的垃圾值可能让 connect 行为异常
    sockaddr_in ai_addr{};
    ai_addr.sin_family = AF_INET;                   // 地址家族：IPv4
    ai_addr.sin_port   = htons(port);               // 端口号：主机字节序 → 网络大端序
    //               └──── htons = Host TO Network Short
    inet_pton(AF_INET, "127.0.0.1", &ai_addr.sin_addr);
    // └─── "presentation to network"
    //      把人类可读的 "127.0.0.1" 转成机器用的 4 字节二进制 0x7F000001
    //      比老式的 inet_addr() 更安全，支持 IPv6

    // ── 步骤 3：拨号 ──
    // connect 是阻塞的，但只阻塞这一次（TCP 三次握手），之后 ipc_fd 就是非阻塞的了
    if (connect(ipc_fd, reinterpret_cast<sockaddr*>(&ai_addr), sizeof(ai_addr)) == -1) {
        std::cerr << "[IPC] 连接 AI 进程失败 (127.0.0.1:" << port << ")" << std::endl;
        close(ipc_fd);
        ipc_fd = -1;  // 重置为"未连接"状态
        return false;
    }

    // ── 步骤 4：设为非阻塞模式 ──
    // fcntl 分两步走，不能直接 F_SETFL | O_NONBLOCK：
    //   第一步 F_GETFL —— 读出当前所有 flag
    //   第二步 F_SETFL —— 在老 flag 基础上追加 O_NONBLOCK，再写回去
    // 直接覆盖式地 F_SETFL 会把其他 flag (如 O_RDWR) 抹掉，fd 就废了
    int flags = fcntl(ipc_fd, F_GETFL, 0);         // 1) 读出现有 flag
    fcntl(ipc_fd, F_SETFL, flags | O_NONBLOCK);      // 2) 追加非阻塞，写回去

    std::cout << "[IPC] 已连接 AI 进程 127.0.0.1:" << port
              << " (fd=" << ipc_fd << ")" << std::endl;
    return true;
}

// ===================================================================
//  把牌桌快照 JSON 打包成 [4字节大端包头][JSON]，非阻塞发给 Python
//  本函数只负责"扔下就跑"——如果 send 返回 EAGAIN，数据留在 send_buffer
//  里，由外层 Epoll 监听到 EPOLLOUT 时再接着发
// ===================================================================
bool IPCClient::SendSnapshot(const std::string& json_data) {
    uint32_t net_len = htonl(json_data.size());
    std::string packet(4, '\0');
    std::memcpy(&packet[0], &net_len, 4);
    packet.append(json_data);

    // 致命：若 send_buffer 里还有旧数据排队，新帧必须 append 到队尾，
    // 绝不能直接 send 插队，否则 TCP 字节流乱序，Python 端切出乱码。
    if (!send_buffer.empty()) {
        send_buffer.append(packet);
        return true;
    }

    ssize_t ret = send(ipc_fd, packet.data(), packet.size(), 0);
    if (ret == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            send_buffer.append(packet.data(), packet.size());
            return true;
        }
        std::cerr << "[IPC] send 出错" << std::endl;
        return false;
    }
    if (ret < static_cast<ssize_t>(packet.size())) {
        send_buffer.append(packet.data() + ret, packet.size() - ret);
    }
    return true;
}

// ===================================================================
//  死循环 recv 直到返回 EAGAIN，数据全部追加到 recv_buffer
//  和 Connection::ReadFromSocket() 一模一样的逻辑
// ===================================================================
bool IPCClient::ReadFromSocket() {
    char temp_buf[1024];
    while (true) {
        ssize_t bytes_read = recv(ipc_fd, temp_buf, sizeof(temp_buf), 0);
        if (bytes_read > 0) {
            recv_buffer.append(temp_buf, bytes_read);
        }
        if (bytes_read == 0) {
            if (!recv_buffer.empty()) return true;
            return false;
        }
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            std::cerr << "[IPC] recv 出错" << std::endl;
            return false;
        }
    }
}

// ===================================================================
//  从 send_buffer 头部 send，发多少删多少
//  和 Connection::WriteToSocket() 一模一样的逻辑
// ===================================================================
bool IPCClient::FlushSendBuffer() {
    while (!send_buffer.empty()) {
        ssize_t sent = send(ipc_fd, send_buffer.data(), send_buffer.size(), 0);
        if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            std::cerr << "[IPC] send 出错" << std::endl;
            return false;
        }
        send_buffer.erase(0, sent);
    }
    return true;
}

// ===================================================================
//  从 recv_buffer 中切出一条完整的 AI 决策 JSON
//  逻辑和 Connection::ExtractMessage() 一模一样：
//    检查前 4 字节 → ntohl 得 body_length → 验证收全 → substr 切割 → erase 销毁
//  只是数据来源不同：Connection 来自外网玩家，IPCClient 来自本地 Python
// ===================================================================
std::string IPCClient::ExtractAIDecision() {
    if (recv_buffer.size() < 4) return "";

    uint32_t raw;
    std::memcpy(&raw, recv_buffer.data(), 4);
    uint32_t body_length = ntohl(raw);

    if (body_length > MAX_PACKET_SIZE) {
        bad_packet = true;
        return "";
    }

    if (recv_buffer.size() < 4 + body_length) return "";

    std::string decision = recv_buffer.substr(4, body_length);
    recv_buffer.erase(0, 4 + body_length);
    return decision;
}
