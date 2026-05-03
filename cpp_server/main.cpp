#include "EpollServer.h"

int main() {

    // 1. 老板先建立物理世界（司令部）
    GameWorld my_world;
    // 实例化我们的高并发服务器，并指定在 8080 房间营业
    EpollServer server(8080,&my_world);

    // 尝试开店（执行 socket -> bind -> listen -> epoll_create1）
    if (server.Start()) {
        // 如果开店成功，大堂经理正式开始死循环接客！
        server.Loop();
    }

    return 0;
}
