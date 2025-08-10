#include "Straf/Audio.h"
#include "Straf/Logging.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <ks.h>
#include <ksmedia.h>
#include <propvarutil.h>

#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace Straf {

namespace {
    struct CoInit {
        CoInit(){ CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
        ~CoInit(){ CoUninitialize(); }
    };

    static std::string WToUtf8(const wchar_t* w){
        if (!w) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s; if (len <= 0) return s; s.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
        return s;
    }

    // Downmix to mono and linear-resample to outRate.
    static void DownmixAndResample(const float* in, size_t inFrames, int inChannels, int inRate,
                                   std::vector<float>& out, int outRate){
        if (inFrames == 0){ out.clear(); return; }
        // Downmix to mono
        std::vector<float> mono;
        mono.resize(inFrames);
        if (inChannels <= 1){
            std::copy(in, in + inFrames, mono.begin());
        } else {
            for (size_t i = 0; i < inFrames; ++i){
                double acc = 0.0;
                const float* frame = in + i * inChannels;
                for (int ch = 0; ch < inChannels; ++ch){ acc += frame[ch]; }
                mono[i] = static_cast<float>(acc / inChannels);
            }
        }
        if (inRate == outRate){ out = std::move(mono); return; }
        // Linear resample
        const double ratio = static_cast<double>(outRate) / static_cast<double>(inRate);
        const size_t outFrames = static_cast<size_t>(std::floor(mono.size() * ratio));
        out.resize(outFrames);
        for (size_t i = 0; i < outFrames; ++i){
            const double pos = static_cast<double>(i) / ratio;
            const size_t i0 = static_cast<size_t>(pos);
            const size_t i1 = std::min(i0 + 1, mono.size() - 1);
            const double t = pos - static_cast<double>(i0);
            out[i] = static_cast<float>((1.0 - t) * mono[i0] + t * mono[i1]);
        }
    }
}

class AudioWasapi final : public IAudioSource {
public:
    bool Initialize(int sampleRate, int channels) override {
        targetRate_ = sampleRate;
        targetChannels_ = channels;
        if (targetRate_ <= 0 || targetChannels_ <= 0){
            LogError("AudioWasapi invalid params (%d Hz, %d ch)", sampleRate, channels);
            return false;
        }
        return true; // Actual device init happens in Start() on the worker thread/APT.
    }

    void Start(AudioCallback onAudio) override {
        if (running_) return;
        stop_ = false;
        running_ = true;
        worker_ = std::thread([this, onAudio]{
            CoInit _co; // ensure COM apartment in this thread

            // Discover default capture endpoint
            ComPtr<IMMDeviceEnumerator> deviceEnumerator;
            HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&deviceEnumerator));
            if (FAILED(hr)) {
                LogError("MMDeviceEnumerator CoCreateInstance failed: 0x%08X", hr);
                running_ = false; return;
            }
            ComPtr<IMMDevice> device;
            
            hr = deviceEnumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &device);
            if (FAILED(hr)) hr = deviceEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
            if (FAILED(hr)) {
                LogError("GetDefaultAudioEndpoint failed: 0x%08X", hr);
                running_ = false; return;
            }

            // Diagnostics: endpoint name/ID
            {
                ComPtr<IPropertyStore> store;
                HRESULT phr = device->OpenPropertyStore(STGM_READ, &store);
                if (SUCCEEDED(phr) && store){
                    PROPVARIANT pv; PropVariantInit(&pv);
                    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR){
                        auto name = WToUtf8(pv.pwszVal);
                        LogInfo("WASAPI endpoint: %s", name.c_str());
                    }
                    PropVariantClear(&pv);
                } else {
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(device->GetId(&id)) && id){
                        auto sid = WToUtf8(id);
                        LogInfo("WASAPI endpoint ID: %s", sid.c_str());
                        CoTaskMemFree(id);
                    }
                }
            }

            // Activate IAudioClient
            ComPtr<IAudioClient> client;
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
            if (FAILED(hr)) { LogError("Activate IAudioClient failed: 0x%08X", hr); running_ = false; return; }

            // Get mix format
            WAVEFORMATEX* mix = nullptr;
            hr = client->GetMixFormat(&mix);
            if (FAILED(hr) || !mix){ LogError("GetMixFormat failed: 0x%08X", hr); running_ = false; return; }

            const int inRate = static_cast<int>(mix->nSamplesPerSec);
            const int inChannels = static_cast<int>(mix->nChannels);
            const bool isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                                 (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                                  reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

            // Initialize event-driven shared-mode capture
            REFERENCE_TIME dur = 10000000; // 1s
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     dur, 0, mix, nullptr);
            if (FAILED(hr)) {
                LogError("IAudioClient Initialize failed: 0x%08X", hr);
                CoTaskMemFree(mix);
                running_ = false; return;
            }

        // Diagnostics: buffer size and mix/output format
        UINT32 bufferFrames = 0; client->GetBufferSize(&bufferFrames);
        LogInfo("WASAPI mix: %d Hz, %d ch, float=%d; bufferFrames=%u; output: %d Hz, %d ch",
            inRate, inChannels, isFloat ? 1 : 0, bufferFrames, targetRate_, targetChannels_);

            // Capture client
            ComPtr<IAudioCaptureClient> capture;
            hr = client->GetService(IID_PPV_ARGS(&capture));
            if (FAILED(hr)) {
                LogError("GetService(IAudioCaptureClient) failed: 0x%08X", hr);
                CoTaskMemFree(mix);
                running_ = false; return;
            }

            // Event handle
            HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!hEvent){ LogError("CreateEvent failed: %lu", GetLastError()); CoTaskMemFree(mix); running_ = false; return; }
            hr = client->SetEventHandle(hEvent);
            if (FAILED(hr)) { LogError("SetEventHandle failed: 0x%08X", hr); CloseHandle(hEvent); CoTaskMemFree(mix); running_ = false; return; }

            // Ready to capture
            hr = client->Start();
            if (FAILED(hr)) { LogError("IAudioClient Start failed: 0x%08X", hr); CloseHandle(hEvent); CoTaskMemFree(mix); running_ = false; return; }

            // Capture loop
            std::vector<float> out;
            std::vector<float> inFloatScratch; // for PCM->float conversion or channel gather
            const int outRate = targetRate_;
            const int outChannels = targetChannels_;

            while(!stop_){
                DWORD wait = WaitForSingleObject(hEvent, 50);
                if (wait != WAIT_OBJECT_0) continue;

                UINT32 packet = 0;
                if (FAILED(capture->GetNextPacketSize(&packet))) continue;
                while(packet > 0){
                    BYTE* pData = nullptr; UINT32 frames = 0; DWORD flags = 0; UINT64 devpos = 0; UINT64 qpcpos = 0;
                    hr = capture->GetBuffer(&pData, &frames, &flags, &devpos, &qpcpos);
                    if (FAILED(hr)) break;

                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

                    // Convert input to float interleaved if needed
                    const float* fin = nullptr;
                    inFloatScratch.clear();
                    if (!silent && frames > 0){
                        if (isFloat){
                            fin = reinterpret_cast<const float*>(pData);
                        } else {
                            // Assume 16-bit PCM if not float
                            const int bytesPerFrame = mix->nBlockAlign;
                            const int bytesPerSample = bytesPerFrame / std::max(1, inChannels);
                            if (bytesPerSample == 2){
                                const int16_t* s = reinterpret_cast<const int16_t*>(pData);
                                inFloatScratch.resize(static_cast<size_t>(frames) * inChannels);
                                const size_t total = static_cast<size_t>(frames) * inChannels;
                                for (size_t i=0;i<total;++i){ inFloatScratch[i] = static_cast<float>(s[i] / 32768.0f); }
                                fin = inFloatScratch.data();
                            } else {
                                // Unsupported format: produce silence
                                inFloatScratch.assign(static_cast<size_t>(frames) * inChannels, 0.0f);
                                fin = inFloatScratch.data();
                            }
                        }
                    } else {
                        // Silent packet -> zeros
                        inFloatScratch.assign(static_cast<size_t>(frames) * std::max(1, inChannels), 0.0f);
                        fin = inFloatScratch.data();
                    }

                    if (frames > 0){
                        DownmixAndResample(fin, frames, inChannels, inRate, out, outRate);
                        if (outChannels <= 1){
                            onAudio(out);
                        } else {
                            // upmix mono to requested channel count
                            std::vector<float> up; up.resize(out.size() * outChannels);
                            for (size_t i=0;i<out.size();++i){
                                for (int ch=0; ch<outChannels; ++ch) up[i*outChannels + ch] = out[i];
                            }
                            onAudio(up);
                        }
                    }

                    capture->ReleaseBuffer(frames);
                    if (FAILED(capture->GetNextPacketSize(&packet))) break;
                }
            }

            client->Stop();
            CloseHandle(hEvent);
            CoTaskMemFree(mix);
            running_ = false;
        });
    }

    void Stop() override {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
        running_ = false;
    }

private:
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    int targetRate_{16000};
    int targetChannels_{1};
};

std::unique_ptr<IAudioSource> CreateAudioWasapi(){
    return std::make_unique<AudioWasapi>();
}

} // namespace Straf
