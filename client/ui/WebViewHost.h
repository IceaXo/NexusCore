#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include <mutex>
#include <vector>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <WebView2.h>

// ================================================================
// Lightweight WebView2 COM callback wrapper (replaces WIL's Callback)
// Wraps a lambda/std::function as a COM callback object via WRL.
// ================================================================
template <typename TInterface, typename TSignature>
struct WvCallback;

template <typename TInterface, typename R, typename... Args>
struct WvCallback<TInterface, R(Args...)>
    : public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
        TInterface>
{
    using Fn = std::function<R(Args...)>;

    WvCallback(Fn fn) : fn_(std::move(fn)) {}

    R STDMETHODCALLTYPE Invoke(Args... args) override {
        return fn_(args...);
    }

    template <typename TLambda>
    static Microsoft::WRL::ComPtr<TInterface> Make(TLambda&& lambda) {
        return Microsoft::WRL::Make<WvCallback>(Fn(std::forward<TLambda>(lambda)));
    }

private:
    Fn fn_;
};

// ================================================================

class WebViewHost {
public:
    using JSCallback = std::function<void(const std::string& json)>;
    using ServerChangeCallback = std::function<void(const std::string& host, uint16_t port)>;

    WebViewHost();
    ~WebViewHost();

    bool Init(HINSTANCE hInstance, const std::wstring& startUrl, int nCmdShow = SW_SHOW);
    void Run();
    void PushState(const std::string& json);
    void SetOnJSMessage(JSCallback cb);
    void SetOnServerChange(ServerChangeCallback cb);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool InitWebView2();
    void OnWebViewReady();
    void FlushStateQueue();

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken msg_token_;

    JSCallback js_callback_;
    ServerChangeCallback server_change_cb_;
    std::wstring start_url_;

    std::mutex state_mutex_;
    std::vector<std::string> state_queue_;

    static WebViewHost* instance_;
    static const UINT WM_FLUSH_STATE = WM_USER + 100;
};
