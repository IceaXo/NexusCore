#include "GameWorld.h"

void GameWorld::AddPlayer(int fd) {
    players[fd] = {0,0};
}

void GameWorld::RemovePlayer(int fd) {
    players.erase(fd);
}

std::string GameWorld::ProcessInput(int fd,const std::string& input) {
    std::cout<<"正在处理玩家："<<fd<<"的动作"<<std::endl;
    Vector2D& pos = players[fd];
    if (input[0] == 'W') pos.y++;
    if (input[0] == 'S') pos.y--;
    if (input[0] == 'A') pos.x--;
    if (input[0] == 'D') pos.x++;
    std::cout<<"战报：玩家"<<fd<<" "<<"移动到了("<<pos.x<<","<<pos.y<<")"<<std::endl<<std::endl;
    return "玩家" + std::to_string(fd) + " 坐标为 (" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ")\n";
}