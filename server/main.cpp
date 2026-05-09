#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include "network/EpollServer.h"
#include "network/HttpServer.h"

static ServerConfig LoadConfig(const std::string& path) {
    ServerConfig cfg;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[main] 无法打开 config.json，使用默认配置" << std::endl;
        return cfg;
    }

    std::stringstream buf;
    buf << file.rdbuf();
    std::string json = buf.str();

    // 简易 JSON 解析（避免引入第三方库）
    auto getInt = [&](const char* key, int default_val) -> int {
        size_t pos = json.find(std::string("\"") + key + "\"");
        if (pos == std::string::npos) return default_val;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return default_val;
        // 跳过冒号和空白
        while (++pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'));
        if (pos >= json.size()) return default_val;
        return std::stoi(json.substr(pos));
    };

    cfg.port = getInt("port", 8080);
    cfg.max_rooms = getInt("max_rooms", 5);
    cfg.default_rounds = getInt("default_rounds", 5);
    cfg.ipc_port = getInt("ipc_port", 8081);
    cfg.http_port = getInt("http_port", 7778);

    std::cout << "[main] 配置加载: port=" << cfg.port
              << " max_rooms=" << cfg.max_rooms
              << " default_rounds=" << cfg.default_rounds
              << " ipc_port=" << cfg.ipc_port
              << " http_port=" << cfg.http_port << std::endl;
    return cfg;
}

int main(int argc, char* argv[]) {
    ServerConfig config = LoadConfig("config.json");

    // 命令行参数
    std::string htmlRoot = "../client/html";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
            config.http_port = std::stoi(argv[++i]);
        }
        if (strcmp(argv[i], "--http-root") == 0 && i + 1 < argc) {
            htmlRoot = argv[++i];
        }
    }

    // 启动 HTTP 静态文件服务器
    HttpServer httpServer;
    httpServer.Start(config.http_port, htmlRoot);

    EpollServer server(config);
    if (server.Start()) {
        server.Loop();
    }

    httpServer.Stop();
    return 0;
}
