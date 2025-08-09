#include "Straf/Audio.h"
#include <thread>
#include <atomic>
#include <chrono>

namespace Straf {

class AudioStub : public IAudioSource {
public:
    bool Initialize(int, int) override { return true; }
    void Start(AudioCallback onAudio) override {
        stop_ = false;
        worker_ = std::thread([this, onAudio]{
            while(!stop_){
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                AudioBuffer buf(320); // 20ms at 16kHz
                onAudio(buf);
            }
        });
    }
    void Stop() override {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
    }
private:
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

IAudioSource* CreateAudioStub(){ return new AudioStub(); }

}
