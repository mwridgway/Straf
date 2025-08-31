#include "Straf/Config.h"
#include "Straf/Logging.h"
#include "Straf/ModernLogging.h"
#include "Straf/Detector.h"
#include "Straf/Audio.h"
#include "Straf/Overlay.h"
#include "Straf/PenaltyManager.h"
#include "Straf/Tray.h"
#include "Straf/STT.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>

namespace fs = std::filesystem;

namespace Straf {
// Global shutdown flag for inter-thread communication
static std::atomic<bool> g_shouldExit{false};
// Forward declarations for factory functions
std::unique_ptr<IAudioSource> CreateAudioSilent();
std::unique_ptr<IOverlayRenderer> CreateOverlayStub(std::shared_ptr<Straf::ILogger> logger);
std::unique_ptr<IPenaltyManager> CreatePenaltyManager(IOverlayRenderer* overlay);
std::optional<AppConfig> LoadConfig(const std::string& path);

struct AppComponents {
    std::unique_ptr<ITray> tray;
    std::unique_ptr<IOverlayRenderer> overlay;
    std::unique_ptr<IPenaltyManager> penalties;
    std::unique_ptr<IAudioSource> audio;
    std::unique_ptr<ITranscriber> stt;
    std::unique_ptr<ITextDetector> detector;
    AppConfig config;
};

// Get configuration file path with environment variable overrides
fs::path GetConfigurationPath();

// Initialize all application components
std::unique_ptr<AppComponents> InitializeComponents();

// Create and configure audio source based on environment
std::unique_ptr<IAudioSource> CreateConfiguredAudioSource();

// Create and configure STT transcriber based on environment  
std::unique_ptr<ITranscriber> CreateConfiguredTranscriber(const std::vector<std::string>& vocabulary);

// Main application loop
void RunMainLoop(AppComponents& components);
}

static fs::path GetAppDataConfigPath(){
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path) != S_OK) return {};
    fs::path p(path);
    CoTaskMemFree(path);
    try {
        fs::create_directories(p / "Straf");
    } catch (const std::exception&) {
        OutputDebugStringA("Failed to create config directory");
        return {};
    }
    p /= "Straf";
    p /= "config.json";
    return p;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int){
    using namespace Straf;
    
    auto components = InitializeComponents();
    if (!components) {
        MessageBoxW(nullptr, L"Failed to initialize application", L"Straf", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    RunMainLoop(*components);
    
    LogInfo("StrafAgent exiting");
    LoggerFactory::Shutdown(); // Shutdown modern logging system
    ShutdownLogging();
    return 0;
}

namespace Straf {

fs::path GetConfigurationPath() {
    // STRAF_CONFIG_PATH (absolute path override)
    DWORD configPathEnv = GetEnvironmentVariableW(L"STRAF_CONFIG_PATH", nullptr, 0);
    if (configPathEnv > 0) {
        std::wstring w; w.resize(configPathEnv - 1);
        if (GetEnvironmentVariableW(L"STRAF_CONFIG_PATH", w.data(), configPathEnv) == configPathEnv - 1) {
            return fs::path(w);
        }
    }
    
    // STRAF_USE_SAMPLE_CONFIG (development override)
    if (GetEnvironmentVariableW(L"STRAF_USE_SAMPLE_CONFIG", nullptr, 0) > 0) {
        return fs::current_path() / "config.sample.json";
    }
    
    // Default: %AppData%\Straf\config.json, copy sample if missing
    fs::path cfgPath = GetAppDataConfigPath();
    if (!fs::exists(cfgPath)) {
        fs::path local = fs::current_path() / "config.sample.json";
        if (fs::exists(local)) {
            try {
                fs::copy_file(local, cfgPath, fs::copy_options::overwrite_existing);
            } catch (const std::exception&) {
                OutputDebugStringA("Failed to copy config file");
            }
        }
    }
    return cfgPath;
}

std::unique_ptr<IAudioSource> CreateConfiguredAudioSource() {
    DWORD audioSourceEnv = GetEnvironmentVariableW(L"STRAF_AUDIO_SOURCE", nullptr, 0);
    std::wstring src;
    if (audioSourceEnv > 0){ 
        src.resize(audioSourceEnv - 1); 
        GetEnvironmentVariableW(L"STRAF_AUDIO_SOURCE", src.data(), audioSourceEnv); 
    }
    
    std::unique_ptr<IAudioSource> audio;
    if (_wcsicmp(src.c_str(), L"wasapi") == 0){
        audio = CreateAudioWasapi();
        LogInfo("Audio source: WASAPI mic");
    } else {
        audio = CreateAudioSilent();
        LogInfo("Audio source: silent");
    }
    
    if (!audio->Initialize(16000, 1)){
        LogError("Audio initialize failed; falling back to silent");
        audio = CreateAudioSilent();
        audio->Initialize(16000, 1);
    }
    
    return audio;
}

std::unique_ptr<ITranscriber> CreateConfiguredTranscriber(const std::vector<std::string>& vocabulary) {
    DWORD need = GetEnvironmentVariableW(L"STRAF_STT", nullptr, 0);
    std::wstring t;
    if (need > 0){ 
        t.resize(need - 1); 
        GetEnvironmentVariableW(L"STRAF_STT", t.data(), need); 
    }
    
    std::unique_ptr<ITranscriber> stt;
    // if (_wcsicmp(t.c_str(), L"sapi") == 0){
    //     stt = CreateTranscriberSapi();
    //     LogInfo("STT: SAPI");
    // } else if (_wcsicmp(t.c_str(), L"vosk") == 0){
        stt = CreateTranscriberVosk();
        LogInfo("STT: Vosk");
    // } else {
    //     stt = CreateTranscriberStub();
    //     LogInfo("STT: stub");
    // }
    
    // Normalize vocabulary for STT
    std::vector<std::string> sttVocab = vocabulary;
    for (auto& w : sttVocab){
        std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    }
    
    if (!stt->Initialize(sttVocab)){
        LogError("STT initialize failed; falling back to stub");
        stt = CreateTranscriberStub();
        stt->Initialize(sttVocab);
    }
    
    return stt;
}

std::unique_ptr<AppComponents> InitializeComponents() {
    auto components = std::make_unique<AppComponents>();
    
    // Initialize tray first
    components->tray = CreateTray();
    components->tray->Run([]{
        LogInfo("Tray exit requested, shutting down.");
        g_shouldExit = true;
    });
    
    // Load configuration
    fs::path cfgPath = GetConfigurationPath();
    auto cfg = LoadConfig(cfgPath.string());
    if (!cfg) return nullptr;
    components->config = std::move(*cfg);
    
    // Initialize logging
    InitLogging(components->config.logLevel);
    
    // Initialize modern logging system  
    LogLevel modernLevel = LogLevel::Info; // Default
    if (components->config.logLevel == "error") modernLevel = LogLevel::Error;
    else if (components->config.logLevel == "warn") modernLevel = LogLevel::Warn;
    else if (components->config.logLevel == "info") modernLevel = LogLevel::Info;
    else if (components->config.logLevel == "debug") modernLevel = LogLevel::Debug;
    else if (components->config.logLevel == "trace") modernLevel = LogLevel::Trace;
    
    auto logger = LoggerFactory::CreateLogger("straf", modernLevel, "logs/straf.log");
    LogInfo("StrafAgent starting...");
    
    // Initialize overlay with logger
    components->overlay = CreateOverlayStub(logger);
    if (!components->overlay->Initialize()) {
        LogError("Failed to initialize overlay");
        return nullptr;
    }
    
    // Initialize penalty manager
    components->penalties = CreatePenaltyManager(components->overlay.get());
    components->penalties->Configure(
        components->config.penalty.queueLimit,
        std::chrono::seconds(components->config.penalty.durationSeconds),
        std::chrono::seconds(components->config.penalty.cooldownSeconds)
    );
    
    // Initialize detector for vocabulary filtering
    components->detector = CreateTextAnalysisDetector();
    if (!components->detector->Initialize(components->config.words)) {
        LogError("Failed to initialize detector");
        return nullptr;
    }
    
    // Initialize audio and STT (no vocabulary filtering in STT - detector will handle it)
    components->audio = CreateConfiguredAudioSource();
    components->stt = CreateConfiguredTranscriber({}); // Empty vocabulary - let STT recognize everything
    
    return components;
}

void RunMainLoop(AppComponents& components) {
    // Set up detection callback - detector will call this for vocabulary matches
    DetectionCallback onDetect = [&components](const DetectionResult& r){
        LogInfo("Detected: %s (%.2f)", r.word.c_str(), r.confidence);
        components.penalties->Trigger(r.word);
    };
    
    // Start detector with detection callback
    components.detector->Start(onDetect);
    
    // Start STT with detector pipeline - STT passes recognized text to detector for analysis
    components.stt->Start([&components](const std::string& recognizedText, float conf){
        if (!recognizedText.empty()) {
            LogInfo(("STT recognized: \"" + recognizedText + "\" (confidence: " + std::to_string(conf) + ")").c_str());
            components.detector->AnalyzeText(recognizedText, conf);
        }
    });
    
    // Start audio with no-op callback (STT handles audio internally)
    components.audio->Start([](const AudioBuffer&){ /* intentionally no-op */ });
    
    // Initialize overlay status
    components.overlay->UpdateStatus(components.penalties->GetStarCount(), "");
    
    // // Test mode: Add initial penalties to demonstrate vignette effect progression
    // LogInfo("Starting vignette test: Adding progressive penalties to demonstrate effect");
    // for (int i = 0; i < 10; i++) {
    //     LogInfo("Adding penalty %d/3 for vignette demonstration", i + 1);
    //     components.penalties->Trigger("test_word");
    //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // }
    
    // Main message loop
    MSG msg{};
    auto lastTick = std::chrono::steady_clock::now();
    while (!g_shouldExit){
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (msg.message == WM_QUIT) break;
        
        auto now = std::chrono::steady_clock::now();
        if (now - lastTick > std::chrono::milliseconds(50)){
            components.penalties->Tick();
            lastTick = now;
        }
        Sleep(5);
    }
    
    // Cleanup
    if (components.stt) components.stt->Stop();
    if (components.audio) components.audio->Stop();
    if (components.detector) components.detector->Stop();
}

}
