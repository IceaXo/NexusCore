#include "ui/WebViewHost.h"
#include "network/TcpClient.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // --- Default start URL (HTTP hosting by server, local file as fallback) ---
    std::string serverIP = "8.134.18.58";
    uint16_t serverPort = 7777;
    std::wstring startUrl = L"http://8.134.18.58:7778/p5_ui.html";

    // --- Parse command line ---
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--url" && i + 1 < argc) {
            std::string url = argv[++i];
            startUrl = std::wstring(url.begin(), url.end());
        } else if (arg == "--file" && i + 1 < argc) {
            // Local file fallback: --file p5_ui.html
            std::string path = argv[++i];
            std::string fileUrl = "file:///";
            fileUrl += path;
            for (auto& ch : fileUrl)
                if (ch == '\\') ch = '/';
            startUrl = std::wstring(fileUrl.begin(), fileUrl.end());
        } else if (arg == "--server" && i + 1 < argc) {
            serverIP = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            serverPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }

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
