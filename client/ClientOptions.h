#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>

struct ClientOptions {
    std::string server = "127.0.0.1";
    uint16_t port = 8080;
    std::string ui = "http://127.0.0.1:7778/p5_ui.html";
    bool file = false;
    bool help = false;
};

inline bool ParseClientOptions(const std::vector<std::string>& args,
                               ClientOptions& result, std::string& error) {
    ClientOptions candidate;
    bool explicitUi = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--help" || arg == "-h") { candidate.help = true; continue; }
        if (arg != "--server" && arg != "--port" && arg != "--url" && arg != "--file") {
            error = "Unknown option: " + arg; return false;
        }
        if (++i >= args.size() || args[i].empty() || args[i].compare(0, 2, "--") == 0) {
            error = "Missing value for " + arg; return false;
        }
        const auto& value = args[i];
        if (arg == "--server") candidate.server = value;
        else if (arg == "--port") {
            if (value.find_first_not_of("0123456789") != std::string::npos) {
                error = "Port must be an integer from 1 to 65535"; return false;
            }
            try {
                const auto port = std::stoul(value);
                if (port < 1 || port > 65535) throw std::out_of_range("port");
                candidate.port = static_cast<uint16_t>(port);
            } catch (...) { error = "Port must be an integer from 1 to 65535"; return false; }
        } else {
            candidate.ui = value;
            candidate.file = arg == "--file";
            explicitUi = true;
        }
    }
    if (!explicitUi) candidate.ui = "http://" + candidate.server + ":7778/p5_ui.html";
    result = candidate;
    error.clear();
    return true;
}
