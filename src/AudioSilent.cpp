#include "Straf/Audio.h"
#include <thread>
#include <atomic>
#include <chrono>

namespace Straf {

// AudioSilent: A silent audio generator that produces zero-filled audio buffers
// at regular intervals (20ms, 320 samples at 16kHz). Used as a fallback when
// real microphone capture is unavailable or for testing purposes.
class AudioSilent : public IAudioSource {
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

std::unique_ptr<IAudioSource> CreateAudioSilent(){ 
    return std::make_unique<AudioSilent>(); 
}

}
