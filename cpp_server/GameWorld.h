#pragma once

#include <iostream>
#include <unordered_map>
#include <string>

struct Vector2D {
    int x;
    int y;
};

class GameWorld {
private:
    std::unordered_map<int,Vector2D> players;

public:
    void AddPlayer(int fd);

    void RemovePlayer(int fd);

    std::string ProcessInput(int fd, const std::string& input);
};