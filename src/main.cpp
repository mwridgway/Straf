#include "Straf/Config.h"
#include "Straf/Logging.h"
#include "Straf/Detector.h"
#include "Straf/Audio.h"
#include "Straf/Overlay.h"
#include "Straf/PenaltyManager.h"
#include "Straf/STT.h"
// #include "Straf/ConfigLoader.h" // Removed because the file does not exist; ensure LoadConfig is declared in one of the included headers
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <memory>
#include <thread>
#include <chrono>
#include <unordered_set>

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
    DWORD configPathEnv = GetEnvironmentVariableW(L"STRAF_CONFIG_PATH", nullptr, 0);
    if (configPathEnv > 0) {
        std::wstring w; w.resize(configPathEnv - 1);
        if (GetEnvironmentVariableW(L"STRAF_CONFIG_PATH", w.data(), configPathEnv) == configPathEnv - 1) {
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

    // Audio source selection (for future STT). Use STRAF_AUDIO_SOURCE=wasapi to enable real mic capture.
    std::unique_ptr<IAudioSource> audio;
    {
        DWORD audioSourceEnv = GetEnvironmentVariableW(L"STRAF_AUDIO_SOURCE", nullptr, 0);
        std::wstring src;
        if (audioSourceEnv > 0){ src.resize(audioSourceEnv - 1); GetEnvironmentVariableW(L"STRAF_AUDIO_SOURCE", src.data(), audioSourceEnv); }
        if (_wcsicmp(src.c_str(), L"wasapi") == 0){
            audio = CreateAudioWasapi();
            LogInfo("Audio source: WASAPI mic");
        } else {
            audio = CreateAudioStub();
            LogInfo("Audio source: stub");
        }
        if (!audio->Initialize(16000, 1)){
            LogError("Audio initialize failed; falling back to stub");
            audio = CreateAudioStub();
            audio->Initialize(16000, 1);
        }
        // Start audio with a lightweight no-op callback for now. This primes the pipeline for upcoming STT.
        audio->Start([](const AudioBuffer&){ /* intentionally no-op */ });
    }

    // STT: choose transcriber (STRAF_STT=sapi|stub). Tokens will be matched to configured words.
    std::unique_ptr<ITranscriber> stt;
    {
        DWORD need = GetEnvironmentVariableW(L"STRAF_STT", nullptr, 0);
        std::wstring t;
        if (need > 0){ t.resize(need - 1); GetEnvironmentVariableW(L"STRAF_STT", t.data(), need); }
        if (_wcsicmp(t.c_str(), L"sapi") == 0){
            stt = CreateTranscriberSapi();
            LogInfo("STT: SAPI");
        } else if (_wcsicmp(t.c_str(), L"vosk") == 0){
            stt = CreateTranscriberVosk();
            LogInfo("STT: Vosk");
        } else {
            stt = CreateTranscriberStub();
            LogInfo("STT: stub");
        }
        // Provide vocabulary (for constrained matching in STT if supported)
        std::vector<std::string> sttVocab = cfg->words;
        for (auto& w : sttVocab){
            std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        }
        if (!stt->Initialize(sttVocab)){
            LogError("STT initialize failed; falling back to stub");
            stt = CreateTranscriberStub();
            stt->Initialize(sttVocab);
        }
    }

    // Bridge STT tokens to detector callback directly for now.
    DetectionCallback onDetect = [&](const DetectionResult& r){
        LogInfo("Detected: %s (%.2f)", r.word.c_str(), r.confidence);
        penalties->Trigger(r.word);
    };

    // Build a case-insensitive vocabulary set for matching
    std::unordered_set<std::string> vocabSet;
    for (auto w : cfg->words){
        std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        vocabSet.insert(std::move(w));
    }
    stt->Start([onDetect, vocabSet = std::move(vocabSet)](const std::string& token, float conf){
        std::string t = token;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        // Log every recognized token
        LogInfo("Recognized: %s (%.2f)", t.c_str(), conf);
        if (vocabSet.find(t) != vocabSet.end()){
            onDetect(DetectionResult{t, conf});
        }
    });

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
    if (stt) stt->Stop();
    if (audio) audio->Stop();
    detector->Stop();
    LogInfo("StrafAgent exiting");
    ShutdownLogging();
    return 0;
}
