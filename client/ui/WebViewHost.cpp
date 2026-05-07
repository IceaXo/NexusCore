#include "WebViewHost.h"
#include <wrl/client.h>
#include <wrl/implements.h>
#include <iostream>

static std::string WideToUtf8(LPCWSTR ws) {
    if (!ws || !*ws) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &result[0], needed, nullptr, nullptr);
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
                if (js_callback_) {
                    js_callback_(WideToUtf8(rawMsg));
                }
                CoTaskMemFree(rawMsg);
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
}

void WebViewHost::FlushStateQueue() {
    std::vector<std::string> states;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        states.swap(state_queue_);
    }

    if (!webview_) return;

    for (const auto& json : states) {
        std::wstring wjson(json.begin(), json.end());
        webview_->PostWebMessageAsJson(wjson.c_str());
    }
}
