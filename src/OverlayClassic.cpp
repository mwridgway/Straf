// D3D11 + DirectComposition overlay (topmost, click-through) with Direct2D/DirectWrite rendering
#include "Straf/Overlay.h"

#include "Straf/ModernLogging.h"
#include <fmt/format.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace Straf {

// Returns true if the given environment variable is set to any value
static bool IsEnvSetA(const char* name){
    char buf[2]{};
    return GetEnvironmentVariableA(name, buf, static_cast<DWORD>(std::size(buf))) > 0;
}

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

class OverlayClassic : public IOverlayRenderer {
public:
    OverlayClassic() : comInitialized_(false) {}

    bool Initialize() override {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized_ = true;
        } else if (hr != RPC_E_CHANGED_MODE) {
            Straf::StrafLog(spdlog::level::err, "CoInitializeEx failed: 0x" + fmt::format("{:08X}", hr));
            return false;
        }
        
        if (!registerWindowClass()) return false;
        if (!createWindow()) return false;
        if (!initD3D()) return false;
        if (!initComposition()) return false;
    Straf::StrafLog(spdlog::level::info, "OverlayClassic initialized (D3D11 + DirectComposition)");
        return true;
    }

    void ShowPenalty(const std::string& label) override {
        {
            std::lock_guard<std::mutex> _lk(stateMutex_);
            label_ = label;
            if (starCount_ <= 0) starCount_ = 1; // ensure at least one
        }
        if (!visible_){
            visible_ = true;
            ShowWindow(hwnd_, SW_SHOWNA);
            startRenderLoop();
        }
    }

    void UpdateStatus(int stars, const std::string& label) override {
        std::lock_guard<std::mutex> _lk(stateMutex_);
        starCount_ = stars;
        if (!label.empty()) label_ = label;
    }

    void Hide() override {
        visible_ = false;
        ShowWindow(hwnd_, SW_HIDE);
    }

    ~OverlayClassic(){
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
    int screenCx = GetSystemMetrics(SM_CXSCREEN);
    int screenCy = GetSystemMetrics(SM_CYSCREEN);
    // Always use full-screen overlay window; content uses swap chain size
    int cx = screenCx;
    int cy = screenCy;
    int x = 0;
    int y = 0;

        // Make window reliably click-through:
        //  - WS_EX_TRANSPARENT: let mouse messages pass to windows underneath
        //  - WS_EX_LAYERED + SetLayeredWindowAttributes: ensure proper composition/click-through on all setups
    //  - Keep WS_EX_TOPMOST so overlay stays above by default
    DWORD exStyle = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST;
        hwnd_ = CreateWindowExW(
            exStyle,
            L"StrafOverlayWindow", L"StrafOverlay",
            WS_POPUP,
            x, y, cx, cy,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_) {
            Straf::StrafLog(spdlog::level::err, "Overlay window creation failed: " + std::to_string(GetLastError()));
            return false;
        }
        // Ensure layered attributes are applied (fully opaque visual, but hit-test transparent)
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
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
            Straf::StrafLog(spdlog::level::err, "D3D11CreateDevice failed: 0x" + fmt::format("{:08X}", hr));
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
            Straf::StrafLog(spdlog::level::err, "CreateDXGIFactory2 failed: 0x" + fmt::format("{:08X}", hr));
            return false;
        }
        return true;
    }

    bool initComposition(){
        // Create DirectComposition device
        HRESULT hr = DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcompDevice_));
        if (FAILED(hr)) {
            Straf::StrafLog(spdlog::level::err, "DCompositionCreateDevice failed: 0x" + fmt::format("{:08X}", hr));
            return false;
        }

        // DirectComposition visual tree first
        HRESULT hrTgt = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_);
        if (FAILED(hrTgt)) {
            Straf::StrafLog(spdlog::level::err, "CreateTargetForHwnd failed: 0x" + fmt::format("{:08X}", hrTgt));
            return false;
        }
        HRESULT hrVis = dcompDevice_->CreateVisual(&visual_);
        if (FAILED(hrVis)) {
            Straf::StrafLog(spdlog::level::err, "CreateVisual failed: 0x" + fmt::format("{:08X}", hrVis));
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
            Straf::StrafLog(spdlog::level::info, "FLIP_DISCARD failed, trying FLIP_SEQUENTIAL");
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            hrSc = dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
            if (FAILED(hrSc)) {
                // Final fallback: try minimal settings
                Straf::StrafLog(spdlog::level::info, "FLIP_SEQUENTIAL failed, trying minimal settings");
                desc.Width = 256;
                desc.Height = 64;
                desc.BufferCount = 1;
                desc.Scaling = DXGI_SCALING_NONE;
                hrSc = dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
                if (FAILED(hrSc)) {
                    Straf::StrafLog(spdlog::level::err, "CreateSwapChainForComposition failed: 0x" + fmt::format("{:08X}", hrSc));
                    return false;
                }
            }
        }
        Straf::StrafLog(spdlog::level::info, "SwapChainForComposition created successfully");

        // Create render target view for back buffer
        ComPtr<ID3D11Texture2D> backBuf;
        hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
        if (FAILED(hr)) {
            Straf::StrafLog(spdlog::level::err, "GetBuffer failed: 0x" + fmt::format("{:08X}", hr));
            return false;
        }
        hr = d3dDevice_->CreateRenderTargetView(backBuf.Get(), nullptr, &rtv_);
        if (FAILED(hr)) {
            Straf::StrafLog(spdlog::level::err, "CreateRenderTargetView failed: 0x" + fmt::format("{:08X}", hr));
            return false;
        }

        // Initialize Direct2D/DirectWrite for drawing text and shapes
        if (!initD2D(backBuf.Get())) return false;

        // Connect visual to swap chain
        visual_->SetContent(swapChain_.Get());
        dcompTarget_->SetRoot(visual_.Get());
        hr = dcompDevice_->Commit();
        if (FAILED(hr)) {
            Straf::StrafLog(spdlog::level::err, "DirectComposition Commit failed: 0x" + fmt::format("{:08X}", hr));
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
        // D2D rendering path
        if (!d2dCtx_) return;
        D2D1_SIZE_F sz = d2dCtx_->GetSize();
        d2dCtx_->BeginDraw();
        d2dCtx_->Clear(D2D1::ColorF(0, 0.0f)); // fully transparent

        // Banner rect
        float bannerHeight = sz.height * 0.22f; // taller banner for text + stars
        D2D1_RECT_F banner = D2D1::RectF(0.0f, 0.0f, sz.width, bannerHeight);
        d2dCtx_->FillRectangle(banner, brushBanner_.Get());

        int stars = 0; std::string label;
        {
            std::lock_guard<std::mutex> _lk(stateMutex_);
            stars = starCount_;
            label = label_;
        }
        if (stars < 0) stars = 0; if (stars > 5) stars = 5;

        // Draw up to 5 stars
        float margin = 16.0f;
        float starRadius = bannerHeight * 0.28f;
        float cx = margin + starRadius;
        float cy = banner.top + bannerHeight * 0.58f;
        for (int i = 0; i < 5; ++i){
            bool active = i < stars;
            drawStar(D2D1::Point2F(cx, cy), starRadius, active ? brushStarActive_.Get() : brushStarInactive_.Get(), active);
            cx += starRadius * 2.2f;
        }

        // Draw label text "Gestraf" (or provided label)
        std::wstring text = L"Gestraf";
        // If a custom label was provided, append it for context
        if (!label.empty()){
            text += L"  -  ";
            int needed = MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, nullptr, 0);
            if (needed > 0){
                std::wstring wlabel; wlabel.resize(needed - 1);
                MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, wlabel.data(), needed - 1);
                text += wlabel;
            }
        }
        float textLeft = cx + margin; // after stars
        if (textLeft < banner.right * 0.35f) textLeft = banner.right * 0.35f; // ensure some space
        D2D1_RECT_F textRc = D2D1::RectF(textLeft, banner.top + bannerHeight * 0.15f, banner.right - margin, banner.bottom - margin * 0.5f);
        d2dCtx_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), textFormat_.Get(), textRc, brushText_.Get());

        HRESULT hr = d2dCtx_->EndDraw();
        if (FAILED(hr)){
            // Try to recover by reinitializing the D2D target
            ComPtr<ID3D11Texture2D> backBuf;
            if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf)))){
                initD2D(backBuf.Get());
            }
        }
    }

    bool initD2D(ID3D11Texture2D* backBuffer){
        // Create D2D factory
        HRESULT hr;
        if (!d2dFactory_){
            D2D1_FACTORY_OPTIONS opts{};
#if defined(_DEBUG)
            // opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
            hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, &d2dFactory_);
            if (FAILED(hr)) { Straf::StrafLog(spdlog::level::err, "D2D1CreateFactory failed: 0x" + fmt::format("{:08X}", hr)); return false; }
        }

        // Create DWrite factory
        if (!dwFactory_){
            hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &dwFactory_);
            if (FAILED(hr)) { Straf::StrafLog(spdlog::level::err, "DWriteCreateFactory failed: 0x" + fmt::format("{:08X}", hr)); return false; }
        }

        // Get DXGI device and create D2D device/context
        ComPtr<IDXGIDevice> dxgiDeviceLocal = dxgiDevice_;
        if (!dxgiDeviceLocal){
            hr = d3dDevice_.As(&dxgiDeviceLocal);
            if (FAILED(hr)) return false;
        }
        hr = d2dFactory_->CreateDevice(dxgiDeviceLocal.Get(), &d2dDevice_);
        if (FAILED(hr)) { Straf::StrafLog(spdlog::level::err, "ID2D1Factory1::CreateDevice failed: 0x" + fmt::format("{:08X}", hr)); return false; }
        hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dCtx_);
        if (FAILED(hr)) { Straf::StrafLog(spdlog::level::err, "ID2D1Device::CreateDeviceContext failed: 0x" + fmt::format("{:08X}", hr)); return false; }

        // Create D2D target bitmap from swap chain back buffer
        ComPtr<IDXGISurface> surface;
        hr = backBuffer->QueryInterface(IID_PPV_ARGS(&surface));
        if (FAILED(hr)) return false;
        FLOAT dpiX = 96.0f, dpiY = 96.0f;
        d2dFactory_->GetDesktopDpi(&dpiX, &dpiY);
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            dpiX, dpiY);
        hr = d2dCtx_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &d2dTarget_);
        if (FAILED(hr)) { Straf::StrafLog(spdlog::level::err, "CreateBitmapFromDxgiSurface failed: 0x" + fmt::format("{:08X}", hr)); return false; }
        d2dCtx_->SetTarget(d2dTarget_.Get());

        // Create brushes
        D2D1_COLOR_F bannerCol = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f);
        D2D1_COLOR_F starActive = D2D1::ColorF(D2D1::ColorF::Gold);
        D2D1_COLOR_F starInactive = D2D1::ColorF(0.4f, 0.4f, 0.4f, 0.7f);
        D2D1_COLOR_F textCol = D2D1::ColorF(D2D1::ColorF::White);
        d2dCtx_->CreateSolidColorBrush(bannerCol, &brushBanner_);
        d2dCtx_->CreateSolidColorBrush(starActive, &brushStarActive_);
        d2dCtx_->CreateSolidColorBrush(starInactive, &brushStarInactive_);
        d2dCtx_->CreateSolidColorBrush(textCol, &brushText_);

        // Create text format (try GTA-like font: Pricedown, then Impact, then Arial Black)
        const wchar_t* fonts[] = { L"Pricedown", L"Impact", L"Arial Black", L"Segoe UI" };
        HRESULT lastHr = E_FAIL;
        for (auto f : fonts){
            lastHr = dwFactory_->CreateTextFormat(
                f, nullptr, DWRITE_FONT_WEIGHT_EXTRA_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 48.0f, L"en-us", &textFormat_);
            if (SUCCEEDED(lastHr)) break;
        }
        if (FAILED(lastHr)) { Straf::StrafLog(spdlog::level::err, "DWrite CreateTextFormat failed: 0x" + fmt::format("{:08X}", lastHr)); return false; }
        textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        return true;
    }

    void drawStar(D2D1_POINT_2F center, float r, ID2D1Brush* brush, bool filled){
        // 5-point star geometry
        ComPtr<ID2D1PathGeometry> geo;
        if (FAILED(d2dFactory_->CreatePathGeometry(&geo))) return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(&sink))) return;
        const int points = 5;
        float angleStep = 3.14159265f * 2.0f / points;
        float startAngle = -3.14159265f / 2.0f;
        float rInner = r * 0.5f;
        for (int i = 0; i < points; ++i){
            float a0 = startAngle + i * angleStep;
            float a1 = a0 + angleStep / 2.0f;
            D2D1_POINT_2F p0 = D2D1::Point2F(center.x + r * cosf(a0), center.y + r * sinf(a0));
            D2D1_POINT_2F p1 = D2D1::Point2F(center.x + rInner * cosf(a1), center.y + rInner * sinf(a1));
            if (i == 0) sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(p1);
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        if (filled) d2dCtx_->FillGeometry(geo.Get(), brush);
        d2dCtx_->DrawGeometry(geo.Get(), brush, 2.0f);
    }

private:
    HWND hwnd_{};
    std::atomic<bool> visible_{false};
    std::string label_;
    int starCount_{0};
    bool comInitialized_;
    std::mutex stateMutex_;

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

    // D2D / DWrite
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dCtx_;
    ComPtr<ID2D1Bitmap1> d2dTarget_;
    ComPtr<ID2D1SolidColorBrush> brushBanner_;
    ComPtr<ID2D1SolidColorBrush> brushStarActive_;
    ComPtr<ID2D1SolidColorBrush> brushStarInactive_;
    ComPtr<ID2D1SolidColorBrush> brushText_;
    ComPtr<IDWriteFactory> dwFactory_;
    ComPtr<IDWriteTextFormat> textFormat_;
};

class OverlayNoop : public IOverlayRenderer {
public:
    bool Initialize() override { return true; }
    void ShowPenalty(const std::string&) override {}
    void UpdateStatus(int, const std::string&) override {}
    void Hide() override {}
};

std::unique_ptr<IOverlayRenderer> CreateOverlayClassic(std::shared_ptr<ILogger> logger){
    // For now, OverlayClassic doesn't use logger - can be added later
    (void)logger; // Suppress unused parameter warning
    return std::make_unique<OverlayClassic>();
}

std::unique_ptr<IOverlayRenderer> CreateOverlayStub() {
    if (IsEnvSetA("STRAF_NO_OVERLAY")) {
        StrafLog(spdlog::level::info, "Using no-op overlay (STRAF_NO_OVERLAY set)");
        return std::make_unique<OverlayNoop>();
    }
    char style[32]{};
    if (GetEnvironmentVariableA("STRAF_OVERLAY_STYLE", style, (DWORD)sizeof(style)) > 0){
        std::string s(style);
        for (auto& c : s) c = (char)tolower((unsigned char)c);
        if (s == "bar"){
            return CreateOverlayBar();
        } else if (s == "vignette"){
            return CreateOverlayVignette();
        }
    }
    return CreateOverlayClassic();
}

}
