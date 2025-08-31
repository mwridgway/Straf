#include "Straf/Tray.h"

#include "Straf/resource.h"
#include <windows.h>
#include <shellapi.h>
#include <thread>

namespace Straf {

class TrayWin : public ITray {
public:
    TrayWin() = default;
    ~TrayWin() override { 
        Stop();
    }
    
    void Run(std::function<void()> onExit) override {
        if (running_) return;
        running_ = true;
        onExit_ = std::move(onExit);
        worker_ = std::thread([this] { TrayLoop(); });
    }
    
    void Stop() {
        if (!running_) return;
        running_ = false;
        
        // Signal the tray thread to exit
        if (hwnd_) {
            PostMessageW(hwnd_, WM_QUIT, 0, 0);
        }
        
        // Wait for thread to finish
        if (worker_.joinable()) {
            worker_.join();
        }
        
        RemoveIcon();
    }
    
private:
    NOTIFYICONDATA nid_{};
    HWND hwnd_{};
    std::thread worker_;
    std::function<void()> onExit_;
    std::atomic<bool> running_{false};
    UINT WM_TRAY_{RegisterWindowMessageW(L"StrafTrayMsg")};
    
    void TrayLoop() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = &WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"StrafTrayClass";
        RegisterClassW(&wc);
        
        hwnd_ = CreateWindowW(wc.lpszClassName, L"", 0, 0,0,0,0, HWND_MESSAGE, nullptr, wc.hInstance, this);
        if (!hwnd_) { return; }
        
        AddIcon();
        
        
        MSG msg;
        while (running_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        
        RemoveIcon();
    }
    void AddIcon() {
        nid_.cbSize = sizeof(nid_);
        nid_.hWnd = hwnd_;
        nid_.uID = 1;
        nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid_.uCallbackMessage = WM_TRAY_;
        
        // Try to load custom icon first, fall back to system icon
        nid_.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_STRAF_ICON));
        if (!nid_.hIcon) { nid_.hIcon = LoadIconW(nullptr, IDI_INFORMATION); }
        
        wcscpy_s(nid_.szTip, L"StrafAgent");
        Shell_NotifyIconW(NIM_ADD, &nid_);
        
    }
    void RemoveIcon() {
        if (nid_.hWnd) {
            Shell_NotifyIconW(NIM_DELETE, &nid_);
            nid_.hWnd = nullptr;
        }
    }
    
    void ShowMenu() {
        POINT pt; GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Exit");
        SetForegroundWindow(hwnd_);
        
        
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        
        if (cmd == 1) { if (onExit_) { onExit_(); } }
    }
    static LRESULT CALLBACK WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        TrayWin* self = reinterpret_cast<TrayWin*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!self && msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }
        if (self && msg == self->WM_TRAY_) {
            
            if (lParam == WM_RBUTTONUP) {
                self->ShowMenu();
            }
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

std::unique_ptr<ITray> CreateTray() { return std::make_unique<TrayWin>(); }

} // namespace Straf
