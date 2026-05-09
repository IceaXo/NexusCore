#include "WebViewHost.h"
#include <iostream>

static std::string WideToUtf8(LPCWSTR ws) {
    if (!ws || !*ws) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &result[0], needed, nullptr, nullptr);
    return result;
}

static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &result[0], needed);
    return result;
}

#pragma comment(lib, "ole32.lib")

WebViewHost* WebViewHost::instance_ = nullptr;
const UINT WebViewHost::WM_FLUSH_STATE;

WebViewHost::WebViewHost() {}
WebViewHost::~WebViewHost() { instance_ = nullptr; }

bool WebViewHost::Init(HINSTANCE hInstance, const std::wstring& htmlPath, int nCmdShow) {
    html_path_ = htmlPath;
    instance_ = this;

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        std::cerr << "[WebViewHost] CoInitializeEx failed" << std::endl;
        return false;
    }

    const wchar_t CLASS_NAME[] = L"NexusCoreClient";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(10, 10, 10));
    RegisterClassW(&wc);

    // Calculate total window size to get 1600×900 client area
    RECT rc = {0, 0, 1600, 900};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExW(
        0, CLASS_NAME, L"NexusCore",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd_) {
        std::cerr << "[WebViewHost] CreateWindowEx failed" << std::endl;
        return false;
    }

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    return InitWebView2();
}

void WebViewHost::Run() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void WebViewHost::PushState(const std::string& json) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_queue_.push_back(json);
    PostMessage(hwnd_, WM_FLUSH_STATE, 0, 0);
}

void WebViewHost::SetOnJSMessage(JSCallback cb) {
    js_callback_ = std::move(cb);
}

void WebViewHost::SetOnServerChange(ServerChangeCallback cb) {
    server_change_cb_ = std::move(cb);
}

// ============================================================
// Private
// ============================================================

std::wstring WebViewHost::GetHTMLFullPath(const std::wstring& filename) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    // Exe is at <root>/client/build/<config>/nexus_client.exe
    // HTML is at <root>/client/html/<filename>
    // So from exe dir: ..\..\html\<filename>
    std::wstring candidates[] = {
        std::wstring(exePath) + L"\\" + filename,
        std::wstring(exePath) + L"\\html\\" + filename,
        std::wstring(exePath) + L"\\..\\html\\" + filename,
        std::wstring(exePath) + L"\\..\\..\\html\\" + filename,
    };

    std::wstring best;
    for (const auto& p : candidates) {
        DWORD attr = GetFileAttributesW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            best = p;
            break;
        }
    }
    if (best.empty()) best = candidates[0];

    std::wstring url = L"file:///";
    url += best;
    for (auto& ch : url)
        if (ch == L'\\') ch = L'/';
    return url;
}

LRESULT CALLBACK WebViewHost::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* host = instance_;

    switch (msg) {
    case WM_SIZE:
        if (host && host->controller_) {
            RECT bounds;
            GetClientRect(hwnd, &bounds);
            host->controller_->put_Bounds(bounds);
        }
        return 0;

    case WM_FLUSH_STATE:
        if (host) host->FlushStateQueue();
        return 0;

    case WM_CLOSE:
        if (host && host->controller_) {
            host->controller_->Close();
            host->controller_ = nullptr;
            host->webview_ = nullptr;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        CoUninitialize();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

bool WebViewHost::InitWebView2() {
    auto envHandler = WvCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                                  HRESULT(HRESULT, ICoreWebView2Environment*)>::Make(
        [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result)) {
                std::cerr << "[WebViewHost] WebView2 environment failed: 0x"
                          << std::hex << result << std::dec << std::endl;
                MessageBoxW(hwnd_,
                    L"WebView2 Runtime not found.\n\n"
                    L"Please install Microsoft Edge or the Evergreen WebView2 Runtime.",
                    L"NexusCore — Missing Runtime", MB_ICONERROR);
                PostQuitMessage(1);
                return result;
            }

            auto ctrlHandler = WvCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                                           HRESULT(HRESULT, ICoreWebView2Controller*)>::Make(
                [this](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (FAILED(result)) {
                        std::cerr << "[WebViewHost] Controller creation failed: 0x"
                                  << std::hex << result << std::dec << std::endl;
                        PostQuitMessage(1);
                        return result;
                    }

                    controller_ = ctrl;
                    controller_->get_CoreWebView2(&webview_);
                    OnWebViewReady();
                    return S_OK;
                }
            );

            env->CreateCoreWebView2Controller(hwnd_, ctrlHandler.Get());
            return S_OK;
        }
    );

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr, envHandler.Get()
    );

    if (FAILED(hr)) {
        std::cerr << "[WebViewHost] CreateCoreWebView2EnvironmentWithOptions failed: 0x"
                  << std::hex << hr << std::dec << std::endl;
        return false;
    }
    return true;
}

void WebViewHost::OnWebViewReady() {
    ICoreWebView2Settings* settings = nullptr;
    webview_->get_Settings(&settings);
    if (settings) {
        settings->put_IsScriptEnabled(TRUE);
        settings->put_IsWebMessageEnabled(TRUE);
        settings->put_AreDevToolsEnabled(TRUE);
    }

    // --- JS → C++ bridge ---
    auto msgHandler = WvCallback<ICoreWebView2WebMessageReceivedEventHandler,
                                  HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)>::Make(
        [this](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            LPWSTR rawMsg = nullptr;
            if (SUCCEEDED(args->TryGetWebMessageAsString(&rawMsg)) && rawMsg) {
                std::string msg = WideToUtf8(rawMsg);
                CoTaskMemFree(rawMsg);

                // Intercept SET_SERVER — trigger reconnect
                if (msg.find("\"SET_SERVER\"") != std::string::npos ||
                    msg.find("\"action\":\"SET_SERVER\"") != std::string::npos) {
                    // Extract host
                    std::string host = "127.0.0.1";
                    size_t pos = msg.find("\"host\"");
                    if (pos != std::string::npos) {
                        pos = msg.find('"', pos + 6);
                        if (pos != std::string::npos) {
                            size_t end = msg.find('"', pos + 1);
                            if (end != std::string::npos)
                                host = msg.substr(pos + 1, end - pos - 1);
                        }
                    }
                    // Extract port
                    uint16_t port = 8080;
                    pos = msg.find("\"port\"");
                    if (pos != std::string::npos) {
                        pos = msg.find(':', pos);
                        if (pos != std::string::npos)
                            port = static_cast<uint16_t>(std::stoi(msg.substr(pos + 1)));
                    }
                    if (server_change_cb_) server_change_cb_(host, port);
                } else {
                    if (js_callback_) js_callback_(msg);
                }
            }
            return S_OK;
        }
    );
    webview_->add_WebMessageReceived(msgHandler.Get(), &msg_token_);

    // Navigation failure logging
    auto navHandler = WvCallback<ICoreWebView2NavigationCompletedEventHandler,
                                  HRESULT(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*)>::Make(
        [](ICoreWebView2* /*sender*/, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            BOOL success;
            args->get_IsSuccess(&success);
            if (!success) {
                COREWEBVIEW2_WEB_ERROR_STATUS status;
                args->get_WebErrorStatus(&status);
                std::cerr << "[WebViewHost] Navigation failed, error: " << status << std::endl;
            }
            return S_OK;
        }
    );
    webview_->add_NavigationCompleted(navHandler.Get(), nullptr);

    RECT bounds;
    GetClientRect(hwnd_, &bounds);
    controller_->put_Bounds(bounds);

    std::wstring url = GetHTMLFullPath(html_path_);
    std::wcout << L"[WebViewHost] Loading: " << url << std::endl;
    webview_->Navigate(url.c_str());

    // Flush any server messages that arrived before WebView2 was ready
    FlushStateQueue();
}

void WebViewHost::FlushStateQueue() {
    if (!webview_) return;  // WebView2 not ready yet — keep messages queued

    std::vector<std::string> states;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        states.swap(state_queue_);
    }

    for (const auto& json : states) {
        std::wstring wjson = Utf8ToWide(json);
        webview_->PostWebMessageAsJson(wjson.c_str());
    }
}
