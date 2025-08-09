// Minimal D3D11 + DirectComposition overlay (topmost, click-through, no hooks)
#include "Straf/Overlay.h"
#include "Straf/Logging.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <string>

using Microsoft::WRL::ComPtr;

namespace Straf {

static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch(msg){
        case WM_NCHITTEST:
            return HTTRANSPARENT; // click-through
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

class OverlayD3D11 : public IOverlayRenderer {
public:
    OverlayD3D11() : comInitialized_(false) {}

    bool Initialize() override {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized_ = true;
        } else if (hr != RPC_E_CHANGED_MODE) {
            LogError("CoInitializeEx failed: 0x%08X", hr);
            return false;
        }
        
        if (!registerWindowClass()) return false;
        if (!createWindow()) return false;
        if (!initD3D()) return false;
        if (!initComposition()) return false;
        LogInfo("Overlay initialized (D3D11 + DirectComposition)");
        return true;
    }

    void ShowPenalty(const std::string& label) override {
        label_ = label;
        if (!visible_){
            visible_ = true;
            ShowWindow(hwnd_, SW_SHOWNA);
            startRenderLoop();
        }
    }

    void Hide() override {
        visible_ = false;
        ShowWindow(hwnd_, SW_HIDE);
    }

    ~OverlayD3D11(){
        visible_ = false;
        if (renderThread_.joinable()) renderThread_.join();
        if (dcompDevice_) dcompDevice_->Commit();
        if (hwnd_) DestroyWindow(hwnd_);
        if (comInitialized_) {
            CoUninitialize();
        }
    }

private:
    bool registerWindowClass(){
        WNDCLASSW wc{};
        wc.lpszClassName = L"StrafOverlayWindow";
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        return RegisterClassW(&wc) != 0;
    }

    bool createWindow(){
        int cx = GetSystemMetrics(SM_CXSCREEN);
        int cy = GetSystemMetrics(SM_CYSCREEN);
        hwnd_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"StrafOverlayWindow", L"StrafOverlay",
            WS_POPUP,
            0, 0, cx, cy,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_) {
            LogError("Overlay window creation failed: %lu", GetLastError());
            return false;
        }
        // Transparency is controlled by DirectComposition alpha; no layered attributes needed.
        return true;
    }

    bool initD3D(){
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        // flags |= D3D11_CREATE_DEVICE_DEBUG; // optional
#endif
        D3D_FEATURE_LEVEL flIn[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL flOut{};
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, flIn, ARRAYSIZE(flIn), D3D11_SDK_VERSION,
            &d3dDevice_, &flOut, &d3dCtx_);
        if (FAILED(hr)) {
            LogError("D3D11CreateDevice failed: 0x%08X", hr);
            return false;
        }
        hr = d3dDevice_.As(&dxgiDevice_);
        if (FAILED(hr)) return false;
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice_->GetAdapter(&adapter);
        if (FAILED(hr)) return false;
        // Prefer creating a fresh Factory2
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory_));
        if (FAILED(hr)) {
            LogError("CreateDXGIFactory2 failed: 0x%08X", hr);
            return false;
        }
        return true;
    }

    bool initComposition(){
        // Create DirectComposition device
        HRESULT hr = DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcompDevice_));
        if (FAILED(hr)) {
            LogError("DCompositionCreateDevice failed: 0x%08X", hr);
            return false;
        }

        // DirectComposition visual tree first
        HRESULT hrTgt = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_);
        if (FAILED(hrTgt)) {
            LogError("CreateTargetForHwnd failed: 0x%08X", hrTgt);
            return false;
        }
        HRESULT hrVis = dcompDevice_->CreateVisual(&visual_);
        if (FAILED(hrVis)) {
            LogError("CreateVisual failed: 0x%08X", hrVis);
            return false;
        }

        // Create swap chain for composition - use reasonable overlay size
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 800;  // Reasonable overlay size
        desc.Height = 200;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc = {1, 0};
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;  // Allow scaling for composition
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // Start with more compatible mode
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = 0;

        HRESULT hrSc = dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
        if (FAILED(hrSc)) {
            // Fallback: try with FLIP_SEQUENTIAL
            LogInfo("FLIP_DISCARD failed, trying FLIP_SEQUENTIAL");
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            hrSc = dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
            if (FAILED(hrSc)) {
                // Final fallback: try minimal settings
                LogInfo("FLIP_SEQUENTIAL failed, trying minimal settings");
                desc.Width = 256;
                desc.Height = 64;
                desc.BufferCount = 1;
                desc.Scaling = DXGI_SCALING_NONE;
                hrSc = dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
                if (FAILED(hrSc)) {
                    LogError("CreateSwapChainForComposition failed: 0x%08X", hrSc);
                    return false;
                }
            }
        }
        LogInfo("SwapChainForComposition created successfully");

        // Create render target view for back buffer
        ComPtr<ID3D11Texture2D> backBuf;
        hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
        if (FAILED(hr)) {
            LogError("GetBuffer failed: 0x%08X", hr);
            return false;
        }
        hr = d3dDevice_->CreateRenderTargetView(backBuf.Get(), nullptr, &rtv_);
        if (FAILED(hr)) {
            LogError("CreateRenderTargetView failed: 0x%08X", hr);
            return false;
        }

        // Connect visual to swap chain
        visual_->SetContent(swapChain_.Get());
        dcompTarget_->SetRoot(visual_.Get());
        hr = dcompDevice_->Commit();
        if (FAILED(hr)) {
            LogError("DirectComposition Commit failed: 0x%08X", hr);
            return false;
        }
        return true;
    }

    void startRenderLoop(){
        if (renderThread_.joinable()) return;
        renderThread_ = std::thread([this]{
            while (visible_){
                drawFrame();
                // Present with sync interval 1 for ~60fps; OK for MVP
                swapChain_->Present(1, 0);
                dcompDevice_->Commit();
                Sleep(16);
            }
        });
    }

    void drawFrame(){
        // Clear to transparent, then draw a semi-transparent banner at top via a second clear using viewport
        FLOAT clearAll[4] = {0.f, 0.f, 0.f, 0.f}; // fully transparent
        d3dCtx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
        d3dCtx_->ClearRenderTargetView(rtv_.Get(), clearAll);

        // Banner area: set viewport to top 12% of screen
        ComPtr<ID3D11Texture2D> backBuf;
        if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf)))){
            D3D11_TEXTURE2D_DESC td{}; backBuf->GetDesc(&td);
            D3D11_VIEWPORT vp{}; 
            vp.TopLeftX = 0; vp.TopLeftY = 0; 
            vp.Width = static_cast<float>(td.Width);
            vp.Height = static_cast<float>(td.Height) * 0.12f;
            vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
            d3dCtx_->RSSetViewports(1, &vp);
            // Clear viewport with semi-transparent black (premultiplied alpha OK with zero RGB)
            FLOAT banner[4] = {0.f, 0.f, 0.f, 0.5f};
            d3dCtx_->ClearRenderTargetView(rtv_.Get(), banner);
        }
    }

private:
    HWND hwnd_{};
    std::atomic<bool> visible_{false};
    std::string label_;
    bool comInitialized_;

    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dCtx_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<IDXGIFactory2> dxgiFactory_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;

    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> visual_;

    std::thread renderThread_;
};

std::unique_ptr<IOverlayRenderer> CreateOverlayStub(){ 
    return std::make_unique<OverlayD3D11>(); 
}

}
