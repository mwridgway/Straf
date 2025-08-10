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
// Forward declarations for factory functions
std::unique_ptr<IAudioSource> CreateAudioStub();
std::unique_ptr<IDetector> CreateDetectorStub();
std::unique_ptr<IOverlayRenderer> CreateOverlayStub();
std::unique_ptr<IPenaltyManager> CreatePenaltyManager(IOverlayRenderer* overlay);
std::optional<AppConfig> LoadConfig(const std::string& path);
std::optional<AppConfig> LoadConfig(const std::string& path);
}

static fs::path GetAppDataConfigPath(){
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path) != S_OK) return {};
    fs::path p(path);
    CoTaskMemFree(path);
    try {
        fs::create_directories(p / "Straf");
    } catch (const std::exception& e) {
        // Can't use LogError here since logging might not be initialized yet
        OutputDebugStringA("Failed to create config directory");
        return {};
    }
    p /= "Straf";
    p /= "config.json";
    return p;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int){
    using namespace Straf;

    // Load config with overrides:
    // - STRAF_CONFIG_PATH: absolute path to a config file to load directly
    // - STRAF_USE_SAMPLE_CONFIG: if set, use config.sample.json from the repo folder
    fs::path cfgPath;
    // STRAF_CONFIG_PATH (wide env)
    DWORD need = GetEnvironmentVariableW(L"STRAF_CONFIG_PATH", nullptr, 0);
    if (need > 0) {
        std::wstring w; w.resize(need - 1);
        if (GetEnvironmentVariableW(L"STRAF_CONFIG_PATH", w.data(), need) == need - 1) {
            cfgPath = fs::path(w);
        }
    }
    // STRAF_USE_SAMPLE_CONFIG fallback
    if (cfgPath.empty()){
        if (GetEnvironmentVariableW(L"STRAF_USE_SAMPLE_CONFIG", nullptr, 0) > 0) {
            cfgPath = fs::current_path() / "config.sample.json";
        }
    }
    // Default to %AppData%\Straf\config.json, copying sample if missing
    if (cfgPath.empty()){
        cfgPath = GetAppDataConfigPath();
        if (!fs::exists(cfgPath)) {
            fs::path local = fs::current_path() / "config.sample.json";
            if (fs::exists(local)) {
                try {
                    fs::copy_file(local, cfgPath, fs::copy_options::overwrite_existing);
                } catch (const std::exception&) {
                    // LogError not available yet, but continue anyway
                    OutputDebugStringA("Failed to copy config file");
                }
            }
        }
    }

    auto cfg = LoadConfig(cfgPath.string());
    if (!cfg){
        MessageBoxW(nullptr, L"Failed to load configuration file", L"Straf", MB_OK | MB_ICONERROR);
        return 1;
    }

    InitLogging(cfg->logLevel);
    LogInfo("StrafAgent starting...");

    // Create overlay (style selected via STRAF_OVERLAY_STYLE, or default classic)
    auto overlay = CreateOverlayStub();
    if (!overlay->Initialize()) {
        LogError("Failed to initialize overlay");
        return 1;
    }

    auto penalties = CreatePenaltyManager(overlay.get());
    penalties->Configure(cfg->penalty.queueLimit,
        std::chrono::seconds(cfg->penalty.durationSeconds),
        std::chrono::seconds(cfg->penalty.cooldownSeconds));

    auto detector = CreateDetectorStub();
    if (!detector->Initialize(cfg->words)) {
        LogError("Failed to initialize detector");
        return 1;
    }

    // Ensure initial status is drawn (0 stars -> hidden banner only)
    overlay->UpdateStatus(penalties->GetStarCount(), "");

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
        Sleep(5);
    }

shutdown:
    detector->Stop();
    LogInfo("StrafAgent exiting");
    ShutdownLogging();
    return 0;
}
