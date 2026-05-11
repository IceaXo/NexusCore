#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Init(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
    }

    void Info(const std::string& msg)  { Log("INFO", msg); }
    void Warn(const std::string& msg)  { Log("WARN", msg); }
    void Error(const std::string& msg) { Log("ERROR", msg); }

    // Write to both log file and stdout
    void InfoConsole(const std::string& msg) {
        std::cout << msg << std::endl;
        Info(msg);
    }
    void ErrorConsole(const std::string& msg) {
        std::cerr << msg << std::endl;
        Error(msg);
    }

private:
    void Log(const std::string& level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open()) return;

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm;
        localtime_r(&t, &tm);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d%02d-%02d:%02d:%02d.%03d",
            tm.tm_mon+1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count());

        file_ << "[" << buf << "] [" << level << "] " << msg << std::endl;
        file_.flush();  // Always flush — game server, not high throughput
    }

    std::ofstream file_;
    std::mutex mutex_;
};
