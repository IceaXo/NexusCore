#pragma once

#include <iostream>
#include <unordered_map>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "Connection.h"
#include "../ipc/IPCClient.h"
#include "../game/RoomManager.h"

// 服务器配置
struct ServerConfig {
    int port = 8080;
    int max_rooms = 5;
    int default_rounds = 5;
    int ipc_port = 8081;
    int http_port = 7778;
};

class EpollServer {
private:
    ServerConfig config;
    int server_fd;
    int epoll_fd;
    std::unordered_map<int, Connection> connections;
    IPCClient ipc_client;
    RoomManager room_manager;

    void HandleAccept();
    bool HandleRead(int fd);
    void HandleWrite(int fd);

public:
    explicit EpollServer(const ServerConfig& cfg);
    ~EpollServer();

    bool Start();
    void Loop();
};
