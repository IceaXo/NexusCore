#include "ui/WebViewHost.h"
#include "network/TcpClient.h"
#include <iostream>
#include <string>
#include "ClientOptions.h"

int main(int argc, char* argv[]) {
    // --- Default start URL (HTTP hosting by server, local file as fallback) ---
    ClientOptions options;
    std::string optionError;
    if (!ParseClientOptions(std::vector<std::string>(argv + 1, argv + argc), options, optionError)) {
        std::cerr << optionError << "\nUse --help for usage.\n";
        return 2;
    }
    if (options.help) {
        std::cout << "nexus_client [--server HOST] [--port 1..65535] [--url URL | --file ABSOLUTE_PATH]\n"
                     "Defaults: 127.0.0.1:8080; UI http://HOST:7778/p5_ui.html\n";
        return 0;
    }
    const std::string serverIP = options.server;
    const uint16_t serverPort = options.port;
    if (options.file) {
        for (auto& ch : options.ui) if (ch == '\\') ch = '/';
        options.ui = "file:///" + options.ui;
    }
    const std::wstring startUrl(options.ui.begin(), options.ui.end());

    // --- Create WebView2 host (Win32 window + WebView2 engine) ---
    WebViewHost host;
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    if (!host.Init(hInstance, startUrl)) {
        std::cerr << "[main] WebViewHost initialization failed" << std::endl;
        return 1;
    }

    // --- Create network client ---
    TcpClient tcp;

    // Bridge: TCP → WebView2
    tcp.SetOnMessage([&host](const std::string& json) {
        host.PushState(json);
    });

    // Bridge: WebView2 → TCP
    host.SetOnJSMessage([&tcp](const std::string& json) {
        tcp.Send(json);
    });

    // Bridge: SET_SERVER → reconnect TCP
    host.SetOnServerChange([&tcp, &host](const std::string& hostStr, uint16_t port) {
        std::cout << "[main] Reconnecting to " << hostStr << ":" << port << std::endl;
        tcp.Disconnect();
        if (!tcp.Connect(hostStr, port)) {
            std::cerr << "[main] Could not connect to server at "
                      << hostStr << ":" << port << std::endl;
            host.PushState("{\"type\":\"error\",\"message\":\"Server unreachable\"}");
        }
    });

    // --- Connect to server ---
    if (!tcp.Connect(serverIP, serverPort)) {
        std::cerr << "[main] Could not connect to server at "
                  << serverIP << ":" << serverPort << std::endl;
        std::cerr << "[main] Running in offline mode (mock data only)" << std::endl;
    }

    // --- Run Windows message loop (blocks until window closes) ---
    host.Run();

    tcp.Disconnect();
    return 0;
}
