#pragma once

#include <iostream>
#include <unordered_map>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "Connection.h"
#include "../ipc/IPCClient.h"

class EpollServer {
private:
    int port;
    int server_fd;
    int epoll_fd;
    std::unordered_map<int, Connection> connections;
    IPCClient ipc_client;

    // ET 模式下循环 accept 直到 EAGAIN，设非阻塞，挂 EPOLLIN|EPOLLOUT|EPOLLET
    void HandleAccept();

    // 从 fd 对应的 Connection 收包、切包，断线或检测到恶意包时返回 false
    bool HandleRead(int fd);

    // 从 fd 对应的 Connection 刷 send_buffer
    void HandleWrite(int fd);

public:
    explicit EpollServer(int _port);
    ~EpollServer();

    bool Start();
    void Loop();
};