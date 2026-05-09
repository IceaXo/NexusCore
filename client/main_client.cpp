#include "ui/WebViewHost.h"
#include "network/TcpClient.h"
#include <iostream>
#include <string>

int main() {
    // --- Parse command line (optional: --server <ip> --port <port>) ---
    std::string serverIP = "127.0.0.1";
    uint16_t serverPort = 8080;

    // --- Create WebView2 host (Win32 window + WebView2 engine) ---
    WebViewHost host;
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    if (!host.Init(hInstance, L"p5_ui.html")) {
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
