#include "Straf/Config.h"
#include "Straf/Logging.h"
#include "Straf/Detector.h"
#include "Straf/Audio.h"
#include "Straf/Overlay.h"
#include "Straf/PenaltyManager.h"
// #include "Straf/ConfigLoader.h" // Removed because the file does not exist; ensure LoadConfig is declared in one of the included headers
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <memory>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace Straf {
// Forward simple factories for stubs
IAudioSource* CreateAudioStub();
IDetector* CreateDetectorStub();
IOverlayRenderer* CreateOverlayStub();
IPenaltyManager* CreatePenaltyManager(IOverlayRenderer* overlay);
}

static fs::path GetAppDataConfigPath(){
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path) != S_OK) return {};
    fs::path p(path);
    CoTaskMemFree(path);
    p /= "Straf";
    fs::create_directories(p);
    p /= "config.json";
    return p;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int){
    using namespace Straf;

    // Load config (AppData fallback to sample next to exe)
    fs::path cfgPath = GetAppDataConfigPath();
    if (!fs::exists(cfgPath)) {
        fs::path local = fs::current_path() / "config.sample.json";
        if (fs::exists(local)) fs::copy_file(local, cfgPath, fs::copy_options::overwrite_existing);
    }

    auto cfg = LoadConfig(cfgPath.string());
    if (!cfg){
        MessageBoxW(nullptr, L"Failed to load config.json", L"Straf", MB_OK | MB_ICONERROR);
        return 1;
    }

    InitLogging(cfg->logLevel);
    LogInfo("StrafAgent starting...");

    // Create components (stubs for now)
    std::unique_ptr<IOverlayRenderer> overlay(CreateOverlayStub());
    overlay->Initialize();

    std::unique_ptr<IPenaltyManager> penalties(CreatePenaltyManager(overlay.get()));
    penalties->Configure(cfg->penalty.queueLimit,
        std::chrono::seconds(cfg->penalty.durationSeconds),
        std::chrono::seconds(cfg->penalty.cooldownSeconds));

    std::unique_ptr<IDetector> detector(CreateDetectorStub());
    detector->Initialize(cfg->words);

    detector->Start([&](const DetectionResult& r){
        LogInfo("Detected: %s (%.2f)", r.word.c_str(), r.confidence);
        penalties->Trigger(r.word);
    });

    // Simple message loop with Tick
    MSG msg{};
    auto lastTick = std::chrono::steady_clock::now();
    while (true){
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT) goto shutdown;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        auto now = std::chrono::steady_clock::now();
        if (now - lastTick > std::chrono::milliseconds(50)){
            penalties->Tick();
            lastTick = now;
        }
        Sleep(10);
    }

shutdown:
    detector->Stop();
    LogInfo("StrafAgent exiting");
    return 0;
}
