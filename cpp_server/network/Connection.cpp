#include "Connection.h"
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h> // 提供 recv 函数
#include <cerrno>       // 提供 errno 宏
#include <iostream>

std::string Connection::ExtractMessage() {
    if (recv_buffer.size() < 4) return "";

    uint32_t raw;
    std::memcpy(&raw, recv_buffer.data(), 4);
    uint32_t body_length = ntohl(raw);

    // 防 4GB 内存核弹：超过 MAX_PACKET_SIZE 的包直接标记坏连接，踢掉
    if (body_length > MAX_PACKET_SIZE) {
        bad_packet = true;
        return "";
    }

    if (recv_buffer.size() < 4 + body_length) return "";

    std::string json_str = recv_buffer.substr(4, body_length);
    recv_buffer.erase(0, 4 + body_length);
    return json_str;
}

bool Connection::ReadFromSocket() {
    // 1. 准备一个局部的小桶，比如 char temp_buf[1024];
    char temp_buffer[1024];
    // 2. 开启死循环：while (true) { ... }
    while (true)
    {
        // 3. 抽水：调用 ssize_t bytes_read = recv(fd, temp_buf, sizeof(temp_buf), 0);
        ssize_t bytes_read = recv(fd, temp_buffer, sizeof(temp_buffer), 0);
        // 4. 判断 bytes_read > 0：
        //    把 temp_buf 里前 bytes_read 个字节，用 append() 追加到 recv_buffer 里。
        if (bytes_read > 0) {
            recv_buffer.append(temp_buffer,bytes_read);
        }
            // 5. 判断 bytes_read == 0：对端关闭连接
        //    如果缓冲区还有余粮，先让 ExtractMessage 消化完
        if (bytes_read == 0) {
            if (!recv_buffer.empty()) return true;
            return false;
        }
        // 6. 判断 bytes_read == -1：
        if (bytes_read == -1){
            //    用 if (errno == EAGAIN || errno == EWOULDBLOCK) 判定。
            //    如果是，说明水抽干了，完美收工，return true;
            //    如果不是，说明出错了，打印个错误日志，return false;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            else {
                std::cerr<<"抽水出错"<<std::endl;
                return false;
            }
        }
    }
}

bool Connection::WriteToSocket(){
    // ET 模式必须循环 send 直到 EAGAIN，否则部分发送后残留数据
    // 可能因 socket 仍可写而永不触发新的 EPOLLOUT
    while (!send_buffer.empty()) {
        ssize_t sent = send(fd, send_buffer.data(), send_buffer.size(), 0);
        if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            std::cerr << "发送出错 fd=" << fd << std::endl;
            return false;
        }
        send_buffer.erase(0, sent);
    }
    return true;
}