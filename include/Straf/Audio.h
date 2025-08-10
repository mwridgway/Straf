#pragma once
#include <functional>
#include <vector>
#include <memory>

namespace Straf {

using AudioBuffer = std::vector<float>; // mono, 16kHz
using AudioCallback = std::function<void(const AudioBuffer&)>;

class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool Initialize(int sampleRate, int channels) = 0;
    virtual void Start(AudioCallback onAudio) = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IAudioSource> CreateAudioStub();
// WASAPI-based microphone capture (shared mode), outputs mono 16kHz float frames (20ms typical)
std::unique_ptr<IAudioSource> CreateAudioWasapi();

}
