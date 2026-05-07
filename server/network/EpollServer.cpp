#include "EpollServer.h"
#include <cerrno>       // errno, EAGAIN
#include <ctime>        // time(nullptr) for heartbeat
#include <fcntl.h>      // fcntl, O_NONBLOCK

EpollServer::EpollServer(int _port) : port(_port), server_fd(-1), epoll_fd(-1) {}
EpollServer::~EpollServer() {
    if (server_fd != -1) close(server_fd);
    if (epoll_fd != -1) close(epoll_fd);
}

bool EpollServer::Start() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "买电话机失败了！" << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        std::cerr << "绑定失败" << std::endl;
        return false;
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        std::cerr << "响铃失败" << std::endl;
        return false;
    }

    // ET 模式必须非阻塞，否则 accept 在队列空时会卡死线程
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "雇佣 Epoll 经理失败！" << std::endl;
        return false;
    }

    std::cout << "服务器启动成功！正监听房间号: " << port << std::endl;
    room_manager.SetConnectionMap(&connections);
    return true;
}

// ===================================================================
//  ET 模式下循环 accept 直到 EAGAIN，设非阻塞，挂 EPOLLIN|EPOLLOUT|EPOLLET
//  文档：HandleAccept —— 接收新连接，加入 connections，挂载 EPOLLIN
// ===================================================================
void EpollServer::HandleAccept() {
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "accept 出错" << std::endl;
            break;
        }

        int cflags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, cflags | O_NONBLOCK);

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            std::cerr << "epoll_ctl 注册失败 fd=" << client_fd << std::endl;
            close(client_fd);
            continue;
        }

        std::cout << "接客成功！新玩家号码牌: " << client_fd << std::endl;
        connections.emplace(client_fd, client_fd);
        room_manager.AddPlayer(client_fd);
    }
}

// ===================================================================
//  文档：HandleRead —— ReadFromSocket → 循环 ExtractMessage → RoomManager::OnMessage
// ===================================================================
bool EpollServer::HandleRead(int fd) {
    auto it = connections.find(fd);
    if (it == connections.end()) return false;

    if (!it->second.ReadFromSocket()) {
        std::cout << "玩家 " << fd << " 断开连接" << std::endl;
        room_manager.RemovePlayer(fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        connections.erase(it);
        return false;
    }

    it->second.last_active_time = time(nullptr);

    while (true) {
        std::string json_msg = it->second.ExtractMessage();
        // 检测到恶意超大包头，立刻踢掉
        if (it->second.bad_packet) {
            std::cerr << "玩家 " << fd << " 发送超大非法包，踢掉" << std::endl;
            room_manager.RemovePlayer(fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            connections.erase(it);
            return false;
        }
        if (json_msg.empty()) break;
        std::cout << "收到完整JSON: " << json_msg << std::endl;
        room_manager.OnMessage(fd, json_msg);
    }
    return true;
}

// ===================================================================
//  EPOLLOUT 出口：查 Connection send_buffer 并刷出
// ===================================================================
void EpollServer::HandleWrite(int fd) {
    auto it = connections.find(fd);
    if (it != connections.end()) {
        if (!it->second.WriteToSocket()) {
            std::cout << "玩家 " << fd << " 写出错，断开" << std::endl;
            room_manager.RemovePlayer(fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            connections.erase(it);
        }
    }
}

// ===================================================================
//  主循环：注册 server_fd + IPC，死循环 epoll_wait，按 fd 分发给三个 Handler
// ===================================================================
void EpollServer::Loop() {
    std::cout << "经理上班" << std::endl;

    // 步骤一：把大门挂上 epoll 红黑树（ET 边缘触发）
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        std::cerr << "没挂上红黑树" << std::endl;
        return;
    }

    // 步骤二：连接 Python AI 进程，把 ipc_fd 也挂上 epoll
    if (ipc_client.ConnectToAI(8081)) {
        room_manager.SetIPCClient(&ipc_client);
        epoll_event ipc_ev{};
        ipc_ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ipc_ev.data.fd = ipc_client.ipc_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ipc_client.ipc_fd, &ipc_ev);
        std::cout << "[EpollServer] IPC fd=" << ipc_client.ipc_fd << " 已注册到 epoll" << std::endl;
    }

    const int MAX_EVENTS = 1024;
    epoll_event events[MAX_EVENTS];

    // 步骤三：死循环
    while (true) {
        int num_ready = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (num_ready == -1) {
            std::cerr << "epoll等待出错" << std::endl;
            return;
        }

        // 心跳超时检测：扫描所有连接，踢掉 30s 以上无消息的
        for (auto it = connections.begin(); it != connections.end(); ) {
            if (it->second.IsHeartbeatTimeout()) {
                int stale_fd = it->first;
                std::cout << "玩家 " << stale_fd << " 心跳超时，踢掉" << std::endl;
                room_manager.RemovePlayer(stale_fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, stale_fd, nullptr);
                close(stale_fd);
                it = connections.erase(it);
            } else {
                ++it;
            }
        }

        for (int i = 0; i < num_ready; ++i) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;

            // 挂断/错误：内核已关闭连接或发生异常
            if (revents & (EPOLLHUP | EPOLLERR)) {
                if (fd == ipc_client.ipc_fd) {
                    std::cerr << "[EpollServer] IPC 连接异常 (HUP/ERR)" << std::endl;
                } else if (fd != server_fd) {
                    HandleRead(fd); // HandleRead 内部发现断线会清理
                }
                continue;
            }

            if (fd == server_fd) {
                HandleAccept();
            } else if (fd == ipc_client.ipc_fd) {
                if (revents & EPOLLIN) {
                    if (!ipc_client.ReadFromSocket() || ipc_client.bad_packet) {
                        std::cerr << "[EpollServer] IPC 连接断开或恶意包" << std::endl;
                        continue;
                    }
                    std::string decision = ipc_client.ExtractAIDecision();
                    if (!decision.empty()) {
                        std::cout << "[EpollServer] 收到 AI 决策: " << decision << std::endl;
                        room_manager.ApplyAIDecision(decision);
                    }
                }
                if (revents & EPOLLOUT) {
                    ipc_client.FlushSendBuffer();
                }
            } else {
                bool alive = true;
                if (revents & EPOLLIN) {
                    alive = HandleRead(fd);
                }
                // 读失败说明已断线/踢掉，跳过 EPOLLOUT 避免鞭尸
                if (alive && (revents & EPOLLOUT)) {
                    HandleWrite(fd);
                }
            }
        }
    }
}