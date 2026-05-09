#pragma once
#include <string>
#include <thread>
#include <atomic>

// 极简 HTTP 静态文件服务器，在独立线程中运行
class HttpServer {
public:
    bool Start(int port, const std::string& rootDir);
    void Stop();

private:
    void Loop();

    std::thread thread_;
    std::atomic<bool> running_{false};
    int port_ = 7778;
    std::string root_dir_;
};
