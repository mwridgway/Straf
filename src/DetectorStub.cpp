#include "Straf/Detector.h"

#include <windows.h>
#include <thread>
#include <atomic>
#include <chrono>

namespace Straf {

class DetectorStub : public IDetector {
public:
    bool Initialize(const std::vector<std::string>& vocabulary) override {
        words_ = vocabulary;
        return true;
    }
    void Start(DetectionCallback onDetect) override {
        stop_ = false;
        worker_ = std::thread([this, onDetect]{
            // Stub: periodically emit a fake detection for demo
            while(!stop_){
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (stop_) break;
                if (!words_.empty()) {
                    onDetect({words_[0], 0.99f});
                } else {
                    onDetect({"example", 0.99f});
                }
            }
        });
    }
    void Stop() override {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
    }
private:
    std::vector<std::string> words_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

class DetectorNoop : public IDetector {
public:
    bool Initialize(const std::vector<std::string>&) override { return true; }
    void Start(DetectionCallback) override {}
    void Stop() override {}
};

static bool IsEnvEnabled(const wchar_t* name){
    wchar_t dummy[2]{};
    return GetEnvironmentVariableW(name, dummy, 2) > 0;
}

std::unique_ptr<IDetector> CreateDetectorStub(){ 
    if (IsEnvEnabled(L"STRAF_NO_DETECTOR")) {
        LogInfo("Using no-op detector (STRAF_NO_DETECTOR set)");
        return std::make_unique<DetectorNoop>();
    }
    return std::make_unique<DetectorStub>(); 
}

}
