// D3D11 + DirectComposition overlay (topmost, click-through) with Direct2D/DirectWrite rendering - Vignette style
#include "Straf/Overlay.h"
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

// ...existing code...

// Returns true if the given environment variable is set to any value
static bool IsEnvSetA(const char* name){
    char buf[2]{};
    return GetEnvironmentVariableA(name, buf, static_cast<DWORD>(std::size(buf))) > 0;
}

static LRESULT CALLBACK OverlayVignetteWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch(msg){
        case WM_NCHITTEST:
            return HTTRANSPARENT; // click-through
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

class OverlayVignette : public IOverlayRenderer {
public:
    OverlayVignette() : comInitialized_(false) {}

    bool Initialize() override {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized_ = true;
        } else if (hr != RPC_E_CHANGED_MODE) { return false; }
        if (!registerWindowClass()) return false;
        if (!createWindow()) return false;
        if (!initD3D()) return false;
        if (!initComposition()) return false;
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
        } else {
        }
    }

    void UpdateStatus(int stars, const std::string& label) override {
        std::lock_guard<std::mutex> _lk(stateMutex_);
        bool starsChanged = (stars != starCount_);
        bool labelChanged = (!label.empty() && label != label_);
        starCount_ = stars;
        if (!label.empty()) label_ = label;
        if (starsChanged || labelChanged) {
        }
        // If we have stars to show but overlay is not visible, make it visible
        if (stars > 0 && !visible_) {
            visible_ = true;
            ShowWindow(hwnd_, SW_SHOWNA);
            startRenderLoop();
        }
        // If no stars and overlay is visible, hide it
        else if (stars == 0 && visible_) {
            visible_ = false;
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    void Hide() override {
        visible_ = false;
        ShowWindow(hwnd_, SW_HIDE);
    }

    ~OverlayVignette(){
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
        wc.lpszClassName = L"StrafOverlayVignetteWindow";
        wc.lpfnWndProc = OverlayVignetteWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        return RegisterClassW(&wc) != 0;
    }

    bool createWindow(){
        int screenCx = GetSystemMetrics(SM_CXSCREEN);
        int screenCy = GetSystemMetrics(SM_CYSCREEN);
        int cx = screenCx;
        int cy = screenCy;
        int x = 0;
        int y = 0;
        DWORD exStyle = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST;
        hwnd_ = CreateWindowExW(
            exStyle,
            L"StrafOverlayVignetteWindow", L"StrafOverlayVignette",
            WS_POPUP,
            x, y, cx, cy,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_) { return false; }
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
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
        if (FAILED(hr)) { return false; }
        hr = d3dDevice_.As(&dxgiDevice_);
        if (FAILED(hr)) return false;
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice_->GetAdapter(&adapter);
        if (FAILED(hr)) return false;
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory_));
        if (FAILED(hr)) { return false; }
        return true;
    }

    bool initComposition(){
        HRESULT hr = DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcompDevice_));
        if (FAILED(hr)) { return false; }
        HRESULT hrTgt = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_);
        if (FAILED(hrTgt)) { return false; }
        HRESULT hrVis = dcompDevice_->CreateVisual(&visual_);
        if (FAILED(hrVis)) { return false; }
        int screenCx = GetSystemMetrics(SM_CXSCREEN);
        int screenCy = GetSystemMetrics(SM_CYSCREEN);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = screenCx;
        desc.Height = screenCy;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc = {1, 0};
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = 0;
        HRESULT hrSc = dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
        if (FAILED(hrSc)) {
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            hrSc = dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
            if (FAILED(hrSc)) { return false; }
        }
        ComPtr<ID3D11Texture2D> backBuf;
        hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
        if (FAILED(hr)) { return false; }
        hr = d3dDevice_->CreateRenderTargetView(backBuf.Get(), nullptr, &rtv_);
        if (FAILED(hr)) { return false; }
        if (!initD2D(backBuf.Get())) return false;
        visual_->SetContent(swapChain_.Get());
        dcompTarget_->SetRoot(visual_.Get());
        hr = dcompDevice_->Commit();
        if (FAILED(hr)) { return false; }
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
        // D2D rendering path - Full-screen progressive vignette effect
        if (!d2dCtx_) return;
        D2D1_SIZE_F sz = d2dCtx_->GetSize();
        d2dCtx_->BeginDraw();
        d2dCtx_->Clear(D2D1::ColorF(0, 0.0f)); // fully transparent

        int stars = 0; std::string label;
        {
            std::lock_guard<std::mutex> _lk(stateMutex_);
            stars = starCount_;
            label = label_;
        }
        if (stars < 0) stars = 0; if (stars > 5) stars = 5;

        // Only draw vignette effect if there are penalties (stars > 0)
        if (stars > 0) {
            drawProgressiveVignette(sz, stars);
        } else {
        }

        // Draw status indicator in top-left corner (always visible)
        drawStatusIndicator(sz, stars, label);

        HRESULT hr = d2dCtx_->EndDraw();
        if (FAILED(hr)){
            ComPtr<ID3D11Texture2D> backBuf;
            if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf)))){
                initD2D(backBuf.Get());
            }
        }
    }

    void drawProgressiveVignette(D2D1_SIZE_F sz, int stars) {
        // Create radial gradient brush for vignette effect
        // Higher star count = more intense vignette = less peripheral vision
        float centerX = sz.width * 0.5f;
        float centerY = sz.height * 0.5f;
        
        // Calculate vignette intensity based on star count
        // 1 star = subtle vignette, 5 stars = very intense
        float intensity = stars / 5.0f; // 0.2 to 1.0
        float baseRadius = std::min(sz.width, sz.height) * 0.6f; // Start with 60% of screen
        float vignetteRadius = baseRadius * (1.0f - (intensity * 0.7f)); // Shrinks with more stars
        
    
        
        // Create radial gradient from center (transparent) to edges (opaque)
        ComPtr<ID2D1RadialGradientBrush> vignetteBrush;
        D2D1_GRADIENT_STOP stops[3];
        stops[0] = { 0.0f, D2D1::ColorF(0, 0, 0, 0.0f) }; // Transparent center
        stops[1] = { 0.7f, D2D1::ColorF(0, 0, 0, 0.0f) }; // Still transparent
        stops[2] = { 1.0f, D2D1::ColorF(0.1f, 0.05f, 0.2f, intensity * 0.8f) }; // Dark vignette edges
        
        ComPtr<ID2D1GradientStopCollection> stopCollection;
        HRESULT hr = d2dCtx_->CreateGradientStopCollection(stops, 3, &stopCollection);
        if (SUCCEEDED(hr)) {
            D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES gradProps = D2D1::RadialGradientBrushProperties(
                D2D1::Point2F(centerX, centerY), // center
                D2D1::Point2F(0, 0), // offset
                vignetteRadius, // radiusX
                vignetteRadius  // radiusY
            );
            hr = d2dCtx_->CreateRadialGradientBrush(gradProps, stopCollection.Get(), &vignetteBrush);
            if (SUCCEEDED(hr)) {
                // Fill entire screen with vignette effect
                D2D1_RECT_F fullScreen = D2D1::RectF(0, 0, sz.width, sz.height);
                d2dCtx_->FillRectangle(fullScreen, vignetteBrush.Get());
            } else {
                
            }
        } else {
            
        }

        // Add additional darkening rings for higher penalty levels
        if (stars >= 3) {
            // Add darker inner ring for severe penalties
            float innerRadius = vignetteRadius * 0.8f;
            float innerIntensity = (stars - 2) / 3.0f; // 0.33 for 3 stars, 1.0 for 5 stars
            
            
            
            ComPtr<ID2D1RadialGradientBrush> innerVignetteBrush;
            D2D1_GRADIENT_STOP innerStops[2];
            innerStops[0] = { 0.0f, D2D1::ColorF(0, 0, 0, 0.0f) };
            innerStops[1] = { 1.0f, D2D1::ColorF(0.2f, 0.1f, 0.3f, innerIntensity * 0.6f) };
            
            ComPtr<ID2D1GradientStopCollection> innerStopCollection;
            hr = d2dCtx_->CreateGradientStopCollection(innerStops, 2, &innerStopCollection);
            if (SUCCEEDED(hr)) {
                D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES innerGradProps = D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(centerX, centerY),
                    D2D1::Point2F(0, 0),
                    innerRadius,
                    innerRadius
                );
                hr = d2dCtx_->CreateRadialGradientBrush(innerGradProps, innerStopCollection.Get(), &innerVignetteBrush);
                if (SUCCEEDED(hr)) {
                    D2D1_RECT_F fullScreen = D2D1::RectF(0, 0, sz.width, sz.height);
                    d2dCtx_->FillRectangle(fullScreen, innerVignetteBrush.Get());
                } else {
                    
                }
            } else {
                
            }
        }
    }

    void drawStatusIndicator(D2D1_SIZE_F sz, int stars, const std::string& label) {
        // Draw compact status indicator in top-left corner
        float indicatorWidth = 300.0f;
        float indicatorHeight = 80.0f;
        float margin = 20.0f;
        
        D2D1_ROUNDED_RECT indicator = D2D1::RoundedRect(
            D2D1::RectF(margin, margin, margin + indicatorWidth, margin + indicatorHeight),
            10.0f, 10.0f
        );
        
        // Background with moderate opacity
        d2dCtx_->FillRoundedRectangle(indicator, brushIndicatorBg_.Get());
        d2dCtx_->DrawRoundedRectangle(indicator, brushBorder_.Get(), 1.5f);

        // Draw stars in indicator
        float starRadius = indicatorHeight * 0.15f;
        float startX = margin + 15.0f + starRadius;
        float starY = margin + indicatorHeight * 0.5f;
        
        for (int i = 0; i < 5; ++i){
            bool active = i < stars;
            drawCompactStar(D2D1::Point2F(startX, starY), starRadius, 
                          active ? brushStarActive_.Get() : brushStarInactive_.Get(), active);
            startX += starRadius * 2.1f;
        }

        // Draw text next to stars
        std::wstring text = L"Gestraf";
        if (!label.empty()){
            text += L" • ";
            int needed = MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, nullptr, 0);
            if (needed > 0){
                std::wstring wlabel; wlabel.resize(needed - 1);
                MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, wlabel.data(), needed - 1);
                text += wlabel;
            }
        }
        
        float textLeft = startX + 10.0f;
        D2D1_RECT_F textRect = D2D1::RectF(
            textLeft, 
            margin + indicatorHeight * 0.2f,
            indicator.rect.right - 10.0f, 
            indicator.rect.bottom - 10.0f
        );
        
        // Text with shadow
        D2D1_RECT_F shadowRect = textRect;
        shadowRect.left += 1.0f; shadowRect.top += 1.0f; 
        shadowRect.right += 1.0f; shadowRect.bottom += 1.0f;
        d2dCtx_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), 
                          compactTextFormat_.Get(), shadowRect, brushTextShadow_.Get());
        d2dCtx_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), 
                          compactTextFormat_.Get(), textRect, brushText_.Get());
    }

    bool initD2D(ID3D11Texture2D* backBuffer){
        HRESULT hr;
        if (!d2dFactory_){
            D2D1_FACTORY_OPTIONS opts{};
#if defined(_DEBUG)
            // opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
            hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, &d2dFactory_);
            if (FAILED(hr)) { return false; }
        }
        if (!dwFactory_){
            hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &dwFactory_);
            if (FAILED(hr)) { return false; }
        }
        ComPtr<IDXGIDevice> dxgiDeviceLocal = dxgiDevice_;
        if (!dxgiDeviceLocal){
            hr = d3dDevice_.As(&dxgiDeviceLocal);
            if (FAILED(hr)) return false;
        }
        hr = d2dFactory_->CreateDevice(dxgiDeviceLocal.Get(), &d2dDevice_);
        if (FAILED(hr)) { return false; }
        hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dCtx_);
        if (FAILED(hr)) { return false; }
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
        if (FAILED(hr)) { return false; }
        d2dCtx_->SetTarget(d2dTarget_.Get());
        D2D1_COLOR_F vignetteCol = D2D1::ColorF(0.1f, 0.05f, 0.2f, 0.75f);
        D2D1_COLOR_F borderCol = D2D1::ColorF(0.5f, 0.3f, 0.7f, 0.8f);
        D2D1_COLOR_F starActive = D2D1::ColorF(D2D1::ColorF::Orange);
        D2D1_COLOR_F starInactive = D2D1::ColorF(0.3f, 0.3f, 0.4f, 0.6f);
        D2D1_COLOR_F textCol = D2D1::ColorF(D2D1::ColorF::White);
        D2D1_COLOR_F textShadowCol = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f);
        d2dCtx_->CreateSolidColorBrush(vignetteCol, &brushVignette_);
        d2dCtx_->CreateSolidColorBrush(borderCol, &brushBorder_);
        d2dCtx_->CreateSolidColorBrush(starActive, &brushStarActive_);
        d2dCtx_->CreateSolidColorBrush(starInactive, &brushStarInactive_);
        d2dCtx_->CreateSolidColorBrush(textCol, &brushText_);
        d2dCtx_->CreateSolidColorBrush(textShadowCol, &brushTextShadow_);
        D2D1_COLOR_F indicatorBgCol = D2D1::ColorF(0.15f, 0.1f, 0.25f, 0.85f);
        d2dCtx_->CreateSolidColorBrush(indicatorBgCol, &brushIndicatorBg_);
        const wchar_t* fonts[] = { L"Calibri", L"Segoe UI", L"Arial" };
        HRESULT lastHr = E_FAIL;
        for (auto f : fonts){
            lastHr = dwFactory_->CreateTextFormat(
                f, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 42.0f, L"en-us", &textFormat_);
            if (SUCCEEDED(lastHr)) break;
        }
        if (FAILED(lastHr)) { return false; }
        textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        lastHr = E_FAIL;
        for (auto f : fonts){
            lastHr = dwFactory_->CreateTextFormat(
                f, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-us", &compactTextFormat_);
            if (SUCCEEDED(lastHr)) break;
        }
        if (FAILED(lastHr)) { return false; }
        compactTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        compactTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        return true;
    }

    void drawVignetteStar(D2D1_POINT_2F center, float r, ID2D1Brush* brush, bool filled){
        // 5-point star geometry with vignette styling (slightly more rounded)
        ComPtr<ID2D1PathGeometry> geo;
        if (FAILED(d2dFactory_->CreatePathGeometry(&geo))) return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(&sink))) return;
        const int points = 5;
        float angleStep = 3.14159265f * 2.0f / points;
        float startAngle = -3.14159265f / 2.0f;
        float rInner = r * 0.45f; // Slightly different inner radius for vignette style
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
        d2dCtx_->DrawGeometry(geo.Get(), brush, 2.5f); // Slightly thicker outline
    }

    void drawCompactStar(D2D1_POINT_2F center, float r, ID2D1Brush* brush, bool filled){
        // Compact 5-point star for status indicator
        ComPtr<ID2D1PathGeometry> geo;
        if (FAILED(d2dFactory_->CreatePathGeometry(&geo))) return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(&sink))) return;
        const int points = 5;
        float angleStep = 3.14159265f * 2.0f / points;
        float startAngle = -3.14159265f / 2.0f;
        float rInner = r * 0.4f; // More compact inner radius
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
        d2dCtx_->DrawGeometry(geo.Get(), brush, 1.5f); // Thinner outline for compact version
    }

private:
    HWND hwnd_{};
    std::atomic<bool> visible_{false};
    std::string label_;
    int starCount_{0};
    bool comInitialized_;
    // logger_ removed
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
    ComPtr<ID2D1SolidColorBrush> brushVignette_;
    ComPtr<ID2D1SolidColorBrush> brushBorder_;
    ComPtr<ID2D1SolidColorBrush> brushStarActive_;
    ComPtr<ID2D1SolidColorBrush> brushStarInactive_;
    ComPtr<ID2D1SolidColorBrush> brushText_;
    ComPtr<ID2D1SolidColorBrush> brushTextShadow_;
    ComPtr<ID2D1SolidColorBrush> brushIndicatorBg_;
    ComPtr<IDWriteFactory> dwFactory_;
    ComPtr<IDWriteTextFormat> textFormat_;
    ComPtr<IDWriteTextFormat> compactTextFormat_;
};

std::unique_ptr<IOverlayRenderer> CreateOverlayVignette(){
    return std::make_unique<OverlayVignette>();
}

} // namespace Straf
