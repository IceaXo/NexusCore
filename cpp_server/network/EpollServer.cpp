#include "EpollServer.h"
#include <cstring>
#include <stdexcept>

EpollServer::EpollServer(int _port) : port(_port), server_fd(-1), epoll_fd(-1) {}
EpollServer::~EpollServer() {
    if (server_fd != -1) close(server_fd);
    if (epoll_fd != -1) close(epoll_fd);
}

bool EpollServer::Start() {
    // 【第一步】：去电信局拿一部 IPv4 的 TCP 电话机
    server_fd = socket(AF_INET,SOCK_STREAM,0);
    if (server_fd == -1) {
        std::cerr << "买电话机失败了！" << std::endl;
        return false;
    }

    // 【极客黑科技】：端口复用 (SO_REUSEADDR)
    // 很多小白写的服务器，一崩溃重启就报错“端口被占用”。加了这两行，允许我们光速重启服务器绑定同一个端口！
    int opt = 1;
    setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    // 【第二步】：准备大楼地址和房间号
    sockaddr_in server_addr{};  
    server_addr.sin_family = AF_INET;  //地址家族：IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;  //监听任意ip
    server_addr.sin_port = htons(port);  //主机字节序转为大端序

    //插网线（Bind）
    if (bind(server_fd,reinterpret_cast<sockaddr*>(&server_addr),sizeof(server_addr)) == -1)
    {
        std::cerr<<"绑定失败"<<std::endl;
        return false;
    }

    //动作四：调成响铃模式（Listen）
    if (listen(server_fd,SOMAXCONN) == -1) {
        std::cerr<<"响铃失败"<<std::endl;
        return false;
    }

    epoll_fd = epoll_create1(0);
    //【第五步：雇佣大堂经理 Epoll】
    if (epoll_fd == -1) {
        std::cerr << "雇佣 Epoll 经理失败！" << std::endl;
        return false;
    }

    std::cout << "服务器启动成功！正监听房间号: " << port << std::endl;
    return true;
}

void EpollServer::Loop() {
    std::cout<<"经理上班"<<std::endl;

    //步骤一：把大门挂上红黑树监控
    epoll_event ev{};

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd,EPOLL_CTL_ADD,server_fd,&ev) == -1) {
        std::cerr<<"没挂上红黑树"<<std::endl;
        return;
    }

    //步骤二：准备“空盘子”数组
    const int MAX_EVENTS = 10;
    epoll_event events[MAX_EVENTS];

    //步骤三：开启死循环与内核休眠
    while (true) {
        int num_ready = epoll_wait(epoll_fd,events,MAX_EVENTS,-1);
        if (num_ready == -1) {
            std::cerr<<"epoll等待出错"<<std::endl;
            return;
        }

        //步骤四：高并发路由的核心分流
        for (int i = 0; i < num_ready; ++i) {
            int current_fd = events[i].data.fd;
            if (current_fd == server_fd) {
                int client_fd = accept(server_fd,nullptr,nullptr);
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd,EPOLL_CTL_ADD,client_fd,&ev);
                std::cout << "接客成功！新玩家号码牌: " << client_fd << std::endl;
            } else if (events[i].events & EPOLLIN) {
                // TODO: 接入 Connection 类处理可读事件
            }
        }
    }
}
