#include "Straf/Overlay.h"
#include "Straf/Logging.h"
#include "Straf/ModernLogging.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace Straf {

static LRESULT CALLBACK BarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch(msg){
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_ERASEBKGND: return 1;
        default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

class OverlayBarImpl : public IOverlayRenderer {
public:
    bool Initialize() override {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE){ LogError("CoInitializeEx failed: 0x%08X", hr); return false; }
        com_ = true;
        WNDCLASSW wc{}; wc.lpszClassName = L"StrafOverlayBar"; wc.lpfnWndProc = BarWndProc; wc.hInstance = GetModuleHandleW(nullptr);
        if (!RegisterClassW(&wc)) return false;
        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);
        int h = 120; int w = sx; int x = 0; int y = sy - h - 20;
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            L"StrafOverlayBar", L"StrafOverlayBar", WS_POPUP, x,y,w,h, nullptr,nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_) return false;
        if (!initD3D()) return false;
        if (!initComposition()) return false;
        LogInfo("OverlayBar initialized");
        return true;
    }
    void ShowPenalty(const std::string& label) override {
        {
            std::lock_guard<std::mutex> lk(mu_);
            label_ = label; if (stars_<=0) stars_=1;
        }
        if (!visible_){ visible_ = true; ShowWindow(hwnd_, SW_SHOWNA); startLoop(); }
    }
    void UpdateStatus(int stars, const std::string& label) override {
        std::lock_guard<std::mutex> lk(mu_);
        stars_ = stars; if (!label.empty()) label_ = label;
    }
    void Hide() override { visible_ = false; ShowWindow(hwnd_, SW_HIDE); }
    ~OverlayBarImpl(){ visible_ = false; if (thr_.joinable()) thr_.join(); if (dcomp_) dcomp_->Commit(); if (hwnd_) DestroyWindow(hwnd_); if (com_) CoUninitialize(); }

private:
    bool initD3D(){
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; D3D_FEATURE_LEVEL flOut{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d_, &flOut, &ctx_);
        if (FAILED(hr)) return false;
        hr = d3d_.As(&dxgi_); if (FAILED(hr)) return false;
        return SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)));
    }
    bool initComposition(){
        HRESULT hr = DCompositionCreateDevice(dxgi_.Get(), IID_PPV_ARGS(&dcomp_)); if (FAILED(hr)) return false;
        hr = dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &target_); if (FAILED(hr)) return false;
        hr = dcomp_->CreateVisual(&visual_); if (FAILED(hr)) return false;
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=800; desc.Height=120; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc={1,0}; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.Scaling=DXGI_SCALING_STRETCH; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
        HRESULT sc = factory_->CreateSwapChainForComposition(d3d_.Get(), &desc, nullptr, &swap_); if (FAILED(sc)) return false;
        ComPtr<ID3D11Texture2D> back; if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false; if (FAILED(d3d_->CreateRenderTargetView(back.Get(), nullptr, &rtv_))) return false;
        if (!initD2D(back.Get())) return false;
        visual_->SetContent(swap_.Get()); target_->SetRoot(visual_.Get()); return SUCCEEDED(dcomp_->Commit());
    }
    bool initD2D(ID3D11Texture2D* back){
        HRESULT hr;
        if (!d2dFactory_){ D2D1_FACTORY_OPTIONS o{}; hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &o, &d2dFactory_); if (FAILED(hr)) return false; }
        if (!dw_){ hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &dw_); if (FAILED(hr)) return false; }
        ComPtr<IDXGISurface> surf; if (FAILED(back->QueryInterface(IID_PPV_ARGS(&surf)))) return false;
        FLOAT dpiX=96.f, dpiY=96.f; d2dFactory_->GetDesktopDpi(&dpiX,&dpiY);
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiY);
        if (FAILED(d2dFactory_->CreateDevice(dxgi_.Get(), &d2dDev_))) return false;
        if (FAILED(d2dDev_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dCtx_))) return false;
        if (FAILED(d2dCtx_->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &d2dTarget_))) return false;
        d2dCtx_->SetTarget(d2dTarget_.Get());
        d2dCtx_->CreateSolidColorBrush(D2D1::ColorF(0.f,0.f,0.f,0.65f), &banner_);
        d2dCtx_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LimeGreen), &accent_);
        d2dCtx_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &text_);
        hr = dw_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 40.f, L"en-us", &fmt_); if (FAILED(hr)) return false;
        fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); return true;
    }
    void startLoop(){ if (thr_.joinable()) return; thr_ = std::thread([this]{ while(visible_){ draw(); swap_->Present(1,0); dcomp_->Commit(); Sleep(16);} }); }
    void draw(){
        if (!d2dCtx_) return; d2dCtx_->BeginDraw(); d2dCtx_->Clear(D2D1::ColorF(0,0));
        D2D1_SIZE_F sz = d2dCtx_->GetSize(); D2D1_RECT_F bar = D2D1::RectF(0,0, sz.width, sz.height);
        d2dCtx_->FillRectangle(bar, banner_.Get());
        int stars; std::string label; { std::lock_guard<std::mutex> lk(mu_); stars = stars_; label = label_; }
        // Progress bar proportional to stars (0..5)
        float ratio = (float)(stars < 0 ? 0 : (stars>5?5:stars)) / 5.0f;
        D2D1_RECT_F prog = D2D1::RectF(0, 0, sz.width * ratio, sz.height);
        d2dCtx_->FillRectangle(prog, accent_.Get());
        // Text
        std::wstring text = L"Straf Bar";
        if (!label.empty()){
            text += L" - "; int need = MultiByteToWideChar(CP_UTF8,0,label.c_str(),-1,nullptr,0); if (need>0){ std::wstring w; w.resize(need-1); MultiByteToWideChar(CP_UTF8,0,label.c_str(),-1,w.data(),need-1); text += w; }
        }
        D2D1_RECT_F rc = D2D1::RectF(16.f, 10.f, sz.width-16.f, sz.height-10.f);
        d2dCtx_->DrawTextW(text.c_str(), (UINT32)text.size(), fmt_.Get(), rc, text_.Get());
        d2dCtx_->EndDraw();
    }
private:
    HWND hwnd_{}; bool com_{false}; std::atomic<bool> visible_{false};
    std::mutex mu_; int stars_{0}; std::string label_;
    ComPtr<ID3D11Device> d3d_; ComPtr<ID3D11DeviceContext> ctx_; ComPtr<IDXGIDevice> dxgi_; ComPtr<IDXGIFactory2> factory_;
    ComPtr<IDXGISwapChain1> swap_; ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<IDCompositionDevice> dcomp_; ComPtr<IDCompositionTarget> target_; ComPtr<IDCompositionVisual> visual_;
    std::thread thr_;
    ComPtr<ID2D1Factory1> d2dFactory_; ComPtr<ID2D1Device> d2dDev_; ComPtr<ID2D1DeviceContext> d2dCtx_; ComPtr<ID2D1Bitmap1> d2dTarget_;
    ComPtr<ID2D1SolidColorBrush> banner_; ComPtr<ID2D1SolidColorBrush> accent_; ComPtr<ID2D1SolidColorBrush> text_;
    ComPtr<IDWriteFactory> dw_; ComPtr<IDWriteTextFormat> fmt_;
};

std::unique_ptr<IOverlayRenderer> CreateOverlayBar(std::shared_ptr<ILogger> logger){ 
    // For now, OverlayBar doesn't use logger - can be added later
    (void)logger; // Suppress unused parameter warning
    return std::make_unique<OverlayBarImpl>(); 
}

} // namespace Straf
