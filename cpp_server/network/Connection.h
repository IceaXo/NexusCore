#pragma once
#include <string>
#include <cstdint>

class Connection {
public:
    int fd;
    std::string recv_buffer; // 蓄水池
    std::string send_buffer; // 发送池 —— 内核缓冲区满时暂存，等 EPOLLOUT 续发

    Connection(int _fd) : fd(_fd) {}

    // 【你的生死考验】：从 recv_buffer 中切出一个完整的 JSON 字符串
    std::string ExtractMessage(); 

    // 这个由 AI 来写：负责死循环抽水直到 EAGAIN
    bool ReadFromSocket(); 
    bool WriteToSocket();
};