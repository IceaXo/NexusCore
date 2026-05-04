#include "Connection.h"
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h> // 提供 recv 函数
#include <cerrno>       // 提供 errno 宏
#include <iostream>

std::string Connection::ExtractMessage() {
    // 步骤 1：探针。包头都没收全就回去继续等
    if (recv_buffer.size() < 4) return "";

    // 步骤 2：解大端序。memcpy 把前 4 字节无损拷出，不依赖内存对齐
    uint32_t raw;
    std::memcpy(&raw, recv_buffer.data(), 4);
    uint32_t body_length = ntohl(raw);
    // 步骤 3：验尸比对。如果 recv_buffer 的总大小 小于 (4 + body_length)
    // 说明身体还没收全，是个半包。返回空字符串 "" 挂起等待。
    if (recv_buffer.size()<4+body_length) return "";
    // 步骤 4：精准切割。满足条件后，利用 substr 从索引 4 开始，截取 body_length 长度。
    // 把它存进一个 std::string json_str 变量里。
    std::string json_str = recv_buffer.substr(4,body_length);
    // 步骤 5：彻底销毁。利用 erase 从索引 0 开始，抹杀掉 (4 + body_length) 个字节。
    recv_buffer.erase(0,4+body_length);
    // 步骤 6：返回你切出来的 json_str。
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
    if (send_buffer.size() == 0) return true;
    ssize_t sent = send(fd, send_buffer.data(), send_buffer.size(), 0);
    if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        std::cerr << "发送出错 fd=" << fd << std::endl;
        return false;
    }
    send_buffer.erase(0, sent);
    return true;
}