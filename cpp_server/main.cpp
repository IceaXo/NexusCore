#include <iostream>
#include "network/EpollServer.h"

int main() {
    EpollServer server(8080);
    if (server.Start()) {
        server.Loop();
    }
    return 0;
}
