#include "HttpServer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

static std::string GetMimeType(const std::string& path) {
    if (path.find(".html") != std::string::npos) return "text/html; charset=utf-8";
    if (path.find(".css")  != std::string::npos) return "text/css; charset=utf-8";
    if (path.find(".js")   != std::string::npos) return "application/javascript; charset=utf-8";
    if (path.find(".png")  != std::string::npos) return "image/png";
    if (path.find(".svg")  != std::string::npos) return "image/svg+xml";
    if (path.find(".json") != std::string::npos) return "application/json; charset=utf-8";
    return "application/octet-stream";
}

static std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string EscapeHtml(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            default:   out += c;
        }
    }
    return out;
}

bool HttpServer::Start(int port, const std::string& rootDir) {
    port_ = port;
    root_dir_ = rootDir;
    running_ = true;
    thread_ = std::thread(&HttpServer::Loop, this);
    std::cout << "[HttpServer] 启动，端口 " << port_ << "，根目录 " << root_dir_ << std::endl;
    return true;
}

void HttpServer::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void HttpServer::Loop() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[HttpServer] socket() 失败" << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[HttpServer] bind() 失败" << std::endl;
        close(fd);
        return;
    }

    if (listen(fd, 16) < 0) {
        std::cerr << "[HttpServer] listen() 失败" << std::endl;
        close(fd);
        return;
    }

    while (running_) {
        sockaddr_in client{};
        socklen_t len = sizeof(client);
        int cfd = accept(fd, (sockaddr*)&client, &len);
        if (cfd < 0) continue;

        char buf[4096];
        int n = recv(cfd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { close(cfd); continue; }
        buf[n] = '\0';

        // 极简 HTTP 解析：提取 GET path
        std::string req(buf, n);
        std::string path = "/";
        size_t gs = req.find("GET ");
        if (gs != std::string::npos) {
            size_t pe = req.find(' ', gs + 4);
            if (pe != std::string::npos)
                path = req.substr(gs + 4, pe - gs - 4);
        }

        // 默认首页
        if (path == "/") path = "/p5_ui.html";

        // 安全检查：拒绝路径遍历
        if (path.find("..") != std::string::npos) {
            std::string body = "403 Forbidden";
            std::string resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body;
            send(cfd, resp.c_str(), resp.size(), 0);
            close(cfd);
            continue;
        }

        std::string filePath = root_dir_ + path;
        std::string content = ReadFile(filePath);

        std::string response;
        if (content.empty()) {
            std::string body = "<html><body><h1>404 Not Found</h1><p>" +
                EscapeHtml(path) + "</p></body></html>";
            response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body;
        } else {
            std::string mime = GetMimeType(path);
            response = "HTTP/1.1 200 OK\r\nContent-Type: " + mime +
                "\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                std::to_string(content.size()) + "\r\n\r\n" + content;
        }

        send(cfd, response.c_str(), response.size(), MSG_NOSIGNAL);
        close(cfd);
    }

    close(fd);
    std::cout << "[HttpServer] 已停止" << std::endl;
}
