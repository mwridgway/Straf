#include "Straf/Tray.h"
#include "Straf/Logging.h"
#include <windows.h>
#include <shellapi.h>
#include <thread>

namespace Straf {

class TrayWin : public ITray {
public:
    TrayWin() = default;
    ~TrayWin() override { RemoveIcon(); }
    void Run(std::function<void()> onExit) override {
        std::thread([this, fn = std::move(onExit)] { TrayLoop(std::move(fn)); }).detach();
    }
private:
    NOTIFYICONDATA nid_{};
    HWND hwnd_{};
    UINT WM_TRAY_{RegisterWindowMessageW(L"StrafTrayMsg")};
    void TrayLoop(std::function<void()> onExit) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = &WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"StrafTrayClass";
        RegisterClassW(&wc);
        hwnd_ = CreateWindowW(wc.lpszClassName, L"", 0, 0,0,0,0, HWND_MESSAGE, nullptr, wc.hInstance, this);
        AddIcon();
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_TRAY_ && msg.lParam == WM_RBUTTONUP) {
                ShowMenu(onExit);
            }
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
        nid_.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
        wcscpy_s(nid_.szTip, L"StrafAgent");
        Shell_NotifyIconW(NIM_ADD, &nid_);
    }
    void RemoveIcon() {
        if (nid_.hWnd) Shell_NotifyIconW(NIM_DELETE, &nid_);
    }
    void ShowMenu(const std::function<void()>& onExit) {
        POINT pt; GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Exit");
        SetForegroundWindow(hwnd_);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        if (cmd == 1) onExit();
    }
    static LRESULT CALLBACK WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        TrayWin* self = reinterpret_cast<TrayWin*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!self && msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }
        if (self && msg == self->WM_TRAY_) {
            PostMessageW(hWnd, msg, wParam, lParam);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

std::unique_ptr<ITray> CreateTray() { return std::make_unique<TrayWin>(); }

} // namespace Straf
