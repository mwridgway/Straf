#include "Straf/STT.h"
#include "Straf/Audio.h"
#include "Straf/Logging.h"

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <memory>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <cstring>
#include <windows.h>

// Vosk headers (assume available via include path when enabled)
#include <vosk_api.h>

namespace Straf {

static std::string ToLower(std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); }); return s; }

class TranscriberVosk : public ITranscriber {
public:
    bool Initialize(const std::vector<std::string>& vocabulary) override {
        vocab_ = vocabulary;
        for (auto& w : vocab_) w = ToLower(w);
        // Model path via env STRAF_VOSK_MODEL, or fallback to ./models/vosk
        wchar_t buf[1024]{}; DWORD n = GetEnvironmentVariableW(L"STRAF_VOSK_MODEL", buf, 1024);
        if (n == 0){ modelPath_ = L"models/vosk"; }
        else { modelPath_ = std::wstring(buf, buf + n); }
        return true;
    }

    void Start(TokenCallback onToken) override {
        if (running_) return;
        cb_ = std::move(onToken);
        running_ = true;
        worker_ = std::thread([this]{ Run(); });
    }

    void Stop() override {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        // Cleanup Vosk
        if (rec_) { vosk_recognizer_free(rec_); rec_ = nullptr; }
        if (spk_) { vosk_spk_model_free(spk_); spk_ = nullptr; }
        if (mod_) { vosk_model_free(mod_); mod_ = nullptr; }
        if (audio_) { audio_->Stop(); audio_.reset(); }
    }

private:
    void Run(){
        vosk_set_log_level(-1);
        std::string mpath(modelPath_.begin(), modelPath_.end());
    LogInfo("Vosk: loading model at %s", mpath.c_str());
        mod_ = vosk_model_new(mpath.c_str());
        if (!mod_) { LogError("Vosk: failed to load model at %s", mpath.c_str()); running_ = false; return; }

        // Optional grammar constraint
        std::string grammar;
        if (!vocab_.empty()){
            std::ostringstream oss; oss << "[";
            for (size_t i=0;i<vocab_.size();++i){ if (i) oss << ","; oss << '"' << vocab_[i] << '"'; }
            oss << "]"; grammar = oss.str();
            rec_ = vosk_recognizer_new_grm(mod_, 16000.0f, grammar.c_str());
            LogInfo("Vosk: using grammar with %zu words", vocab_.size());
        } else {
            rec_ = vosk_recognizer_new(mod_, 16000.0f);
            LogInfo("Vosk: using free dictation (no grammar)");
        }
        if (!rec_) { LogError("Vosk: failed to create recognizer"); running_ = false; return; }

        // Create audio source (prefer WASAPI)
        audio_ = CreateAudioWasapi();
        if (!audio_ || !audio_->Initialize(16000, 1)){
            LogError("Vosk: audio init failed"); running_ = false; return; }

        // Consume audio and feed recognizer
        audio_->Start([this](const AudioBuffer& buf){ OnAudio(buf); });

        while (running_) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    }

    void OnAudio(const AudioBuffer& buf){
        if (!rec_ || !cb_) return;
        // Convert float [-1,1] to int16 for Vosk
        std::vector<int16_t> pcm(buf.size());
        for (size_t i=0;i<buf.size(); ++i){
            float s = std::max(-1.0f, std::min(1.0f, buf[i]));
            pcm[i] = (int16_t)std::lrintf(s * 32767.0f);
        }
        if (vosk_recognizer_accept_waveform(rec_, (const char*)pcm.data(), (int)(pcm.size()*sizeof(int16_t)))){
            const char* j = vosk_recognizer_result(rec_);
            ParseAndEmit(j);
        } else {
            const char* j = vosk_recognizer_partial_result(rec_);
            ParseAndEmit(j); // emit partials too
        }
    }

    void ParseAndEmit(const char* json){
        if (!json || !cb_) return;
        // naive parse for "text":"..."
        const char* t = strstr(json, "\"text\":\"");
        if (!t) return;
        t += 8;
        const char* e = strchr(t, '"');
        if (!e) return;
        std::string phrase(t, e);
        std::string tok;
        for (char c : phrase){
            if (std::isalpha((unsigned char)c)) tok.push_back((char)std::tolower((unsigned char)c));
            else { if (!tok.empty()){ cb_(tok, 0.8f); tok.clear(); } }
        }
        if (!tok.empty()) cb_(tok, 0.8f);
    }

    std::vector<std::string> vocab_;
    std::wstring modelPath_;
    std::unique_ptr<IAudioSource> audio_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    VoskModel* mod_{nullptr};
    VoskRecognizer* rec_{nullptr};
    VoskSpkModel* spk_{nullptr};
    TokenCallback cb_{};
};

std::unique_ptr<ITranscriber> CreateTranscriberVosk(){ return std::make_unique<TranscriberVosk>(); }

}
