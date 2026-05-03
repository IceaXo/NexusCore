#pragma once

#include "GameWorld.h"
#include <iostream>
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <sys/epoll.h>  
#include <unistd.h>




class EpollServer {
private:
    int port;           // 房间号 (端口)
    int server_fd;      // 总机电话号码牌 (监听 Socket)
    int epoll_fd;       // 大堂经理号码牌 (Epoll 句柄)
    GameWorld* world;

public:
    EpollServer(int _port,GameWorld* _world);
    ~EpollServer();

    bool Start(); // 初始化网络大门和 Epoll
    void Loop();  // 死循环接客
};