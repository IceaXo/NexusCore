#pragma once

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>

class EpollServer {
private:
    int port;
    int server_fd;
    int epoll_fd;

public:
    explicit EpollServer(int _port);
    ~EpollServer();

    bool Start();
    void Loop();
};