#pragma once
// M2 T2: 极简 COM 回调适配器。
// vendored WebView2.h 不含官方文档示例里的 Callback<> 模板（那部分在 SDK 的
// sample 头中，nuget 包不随带），项目按需手写。模式统一：堆分配、手工 IUnknown、
// Invoke 转发 std::function。
//
// TU 规则（M2 spike 验证）：包含 WebView2.h 的翻译单元【不得】定义
// WIN32_LEAN_AND_MEAN（它排除 <rpc.h>，MIDL_INTERFACE 宏随之未定义）。
#include "WebView2.h"
#include <functional>

namespace wv2 {

#define WV2_IMPL_IUNKNOWN(Itf)                                                       \
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); } \
    ULONG STDMETHODCALLTYPE Release() override {                                     \
        ULONG r = InterlockedDecrement(&ref_);                                       \
        if (r == 0) delete this;                                                     \
        return r;                                                                    \
    }                                                                                \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** pp) override {      \
        if (riid == __uuidof(IUnknown) || riid == __uuidof(Itf)) {                   \
            *pp = this;                                                              \
            AddRef();                                                                \
            return S_OK;                                                             \
        }                                                                            \
        *pp = nullptr;                                                               \
        return E_NOINTERFACE;                                                        \
    }

struct EnvCreated : ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    ULONG ref_ = 1;
    std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> fn;
    explicit EnvCreated(std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> f)
        : fn(std::move(f)) {}
    WV2_IMPL_IUNKNOWN(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT s, ICoreWebView2Environment* e) override {
        return fn(s, e);
    }
};

struct CtrlCreated : ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    ULONG ref_ = 1;
    std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> fn;
    explicit CtrlCreated(std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> f)
        : fn(std::move(f)) {}
    WV2_IMPL_IUNKNOWN(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT s, ICoreWebView2Controller* c) override {
        return fn(s, c);
    }
};

struct MsgReceived : ICoreWebView2WebMessageReceivedEventHandler {
    ULONG ref_ = 1;
    std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> fn;
    explicit MsgReceived(std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> f)
        : fn(std::move(f)) {}
    WV2_IMPL_IUNKNOWN(ICoreWebView2WebMessageReceivedEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* s,
                                     ICoreWebView2WebMessageReceivedEventArgs* a) override {
        return fn(s, a);
    }
};

struct NavStarting : ICoreWebView2NavigationStartingEventHandler {
    ULONG ref_ = 1;
    std::function<HRESULT(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*)> fn;
    explicit NavStarting(std::function<HRESULT(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*)> f)
        : fn(std::move(f)) {}
    WV2_IMPL_IUNKNOWN(ICoreWebView2NavigationStartingEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* s,
                                     ICoreWebView2NavigationStartingEventArgs* a) override {
        return fn(s, a);
    }
};

struct NewWindow : ICoreWebView2NewWindowRequestedEventHandler {
    ULONG ref_ = 1;
    std::function<HRESULT(ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*)> fn;
    explicit NewWindow(std::function<HRESULT(ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*)> f)
        : fn(std::move(f)) {}
    WV2_IMPL_IUNKNOWN(ICoreWebView2NewWindowRequestedEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* s,
                                     ICoreWebView2NewWindowRequestedEventArgs* a) override {
        return fn(s, a);
    }
};

#undef WV2_IMPL_IUNKNOWN
}  // namespace wv2
