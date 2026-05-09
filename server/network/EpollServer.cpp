#include "EpollServer.h"
#include <cerrno>
#include <ctime>
#include <fcntl.h>

EpollServer::EpollServer(const ServerConfig& cfg)
    : config(cfg), server_fd(-1), epoll_fd(-1) {}

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
    server_addr.sin_port = htons(config.port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        std::cerr << "绑定失败" << std::endl;
        return false;
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        std::cerr << "响铃失败" << std::endl;
        return false;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "雇佣 Epoll 经理失败！" << std::endl;
        return false;
    }

    std::cout << "服务器启动成功！监听端口: " << config.port
              << " 房间池: " << config.max_rooms
              << " 默认局数: " << config.default_rounds << std::endl;
    room_manager.SetConnectionMap(&connections);
    return true;
}

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
        // 新玩家进入大厅，不再自动分配房间
        room_manager.AddToLobby(client_fd);
    }
}

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

void EpollServer::Loop() {
    std::cout << "经理上班" << std::endl;

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        std::cerr << "没挂上红黑树" << std::endl;
        return;
    }

    if (ipc_client.ConnectToAI(config.ipc_port)) {
        room_manager.SetIPCClient(&ipc_client);
        epoll_event ipc_ev{};
        ipc_ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ipc_ev.data.fd = ipc_client.ipc_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ipc_client.ipc_fd, &ipc_ev);
        std::cout << "[EpollServer] IPC fd=" << ipc_client.ipc_fd << " 已注册到 epoll" << std::endl;
    }

    const int MAX_EVENTS = 1024;
    epoll_event events[MAX_EVENTS];

    while (true) {
        int num_ready = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (num_ready == -1) {
            std::cerr << "epoll等待出错" << std::endl;
            return;
        }

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

            if (revents & (EPOLLHUP | EPOLLERR)) {
                if (fd == ipc_client.ipc_fd) {
                    std::cerr << "[EpollServer] IPC 连接异常 (HUP/ERR)" << std::endl;
                } else if (fd != server_fd) {
                    HandleRead(fd);
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
                if (alive && (revents & EPOLLOUT)) {
                    HandleWrite(fd);
                }
            }
        }
    }
}
