#include "Straf/STT.h"
#include "Straf/Audio.h"

#include "Straf/ModernLogging.h"

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
        Straf::StrafLog(spdlog::level::info, "Vosk: Starting transcriber thread");
        worker_ = std::thread([this]{ Run(); });
    }

    void Stop() override {
        if (!running_)
            return;
        Straf::StrafLog(spdlog::level::info, "Vosk: Stopping transcriber");
        running_ = false;
        if (worker_.joinable())
            worker_.join();
        // Cleanup Vosk
        if (rec_) {
            vosk_recognizer_free(rec_);
            rec_ = nullptr;
            Straf::StrafLog(spdlog::level::debug, "Vosk: Freed recognizer");
        }
        if (rec_) {
            vosk_recognizer_free(rec_);
            rec_ = nullptr;
            StrafLog(spdlog::level::trace, "Vosk: Freed recognizer");
        }
        if (spk_) {
            vosk_spk_model_free(spk_);
            spk_ = nullptr;
        }
        if (mod_) {
            vosk_model_free(mod_);
            mod_ = nullptr;
            Straf::StrafLog(spdlog::level::debug, "Vosk: Freed model");
        }
        if (mod_) {
            vosk_model_free(mod_);
            mod_ = nullptr;
            StrafLog(spdlog::level::trace, "Vosk: Freed model");
        }
        if (audio_) {
            audio_->Stop();
            audio_.reset();
            Straf::StrafLog(spdlog::level::debug, "Vosk: Stopped audio");
        }
        if (audio_) {
            audio_->Stop();
            audio_.reset();
            StrafLog(spdlog::level::trace, "Vosk: Stopped audio");
        }
        Straf::StrafLog(spdlog::level::info, "Vosk: Transcriber stopped");
    }

private:
    void Run(){
        vosk_set_log_level(-1);
        
        // Convert wide string path to narrow string safely
        int pathLen = WideCharToMultiByte(CP_UTF8, 0, modelPath_.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string mpath(pathLen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, modelPath_.c_str(), -1, mpath.data(), pathLen, nullptr, nullptr);
        
        Straf::StrafLog(spdlog::level::info, "Vosk: loading model at " + mpath);
        mod_ = vosk_model_new(mpath.c_str());
        if (!mod_) { Straf::StrafLog(spdlog::level::err, "Vosk: failed to load model at " + mpath); running_ = false; return; }

        // Optional grammar constraint
        std::string grammar;
        // if (!vocab_.empty()){
        //     std::ostringstream oss; oss << "[";
        //     for (size_t i=0;i<vocab_.size();++i){ if (i) oss << ","; oss << '"' << vocab_[i] << '"'; }
        //     oss << "]"; grammar = oss.str();
        //     rec_ = vosk_recognizer_new_grm(mod_, 16000.0f, grammar.c_str());
        //     LogInfo("Vosk: using grammar with %zu words", vocab_.size());
        // } else {
            rec_ = vosk_recognizer_new(mod_, 16000.0f);
            Straf::StrafLog(spdlog::level::info, "Vosk: using free dictation (no grammar)");
        // }
        if (!rec_) { Straf::StrafLog(spdlog::level::err, "Vosk: failed to create recognizer"); running_ = false; return; }

        // Create audio source (prefer WASAPI)
        Straf::StrafLog(spdlog::level::debug, "Vosk: Initializing WASAPI audio source");
    StrafLog(spdlog::level::trace, "Vosk: Initializing WASAPI audio source");
        audio_ = CreateAudioWasapi();
        if (!audio_ || !audio_->Initialize(16000, 1)){
            Straf::StrafLog(spdlog::level::err, "Vosk: audio init failed"); running_ = false; return; }
        
        Straf::StrafLog(spdlog::level::debug, "Vosk: Audio initialized successfully, sample rate: 16000Hz");
    StrafLog(spdlog::level::trace, "Vosk: Audio initialized successfully, sample rate: 16000Hz");

        // Consume audio and feed recognizer
        Straf::StrafLog(spdlog::level::debug, "Vosk: Starting audio capture");
    StrafLog(spdlog::level::trace, "Vosk: Starting audio capture");
        audio_->Start([this](const AudioBuffer& buf){ OnAudio(buf); });

        Straf::StrafLog(spdlog::level::debug, "Vosk: Entering main processing loop");
    StrafLog(spdlog::level::trace, "Vosk: Entering main processing loop");
        while (running_) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
        
        Straf::StrafLog(spdlog::level::info, "Vosk: Exited main processing loop");
    }

    void OnAudio(const AudioBuffer& buf){
        if (!rec_ || !cb_) return;
        
        // Log first few audio callbacks to confirm flow
        static int audioCallCount = 0;
        if (audioCallCount < 5) {
            Straf::StrafLog(spdlog::level::debug, "Vosk: Received audio buffer #" + std::to_string(++audioCallCount) + ", size=" + std::to_string(buf.size()) + " samples");
        } else if (audioCallCount == 5) {
            Straf::StrafLog(spdlog::level::debug, "Vosk: Audio flow established (suppressing further audio buffer logs)");
            ++audioCallCount;
        }
        
        // Convert float [-1,1] to int16 for Vosk
        std::vector<int16_t> pcm(buf.size());
        for (size_t i=0;i<buf.size(); ++i){
            float s = std::max(-1.0f, std::min(1.0f, buf[i]));
            pcm[i] = (int16_t)std::lrintf(s * 32767.0f);
        }
        
        if (vosk_recognizer_accept_waveform(rec_, (const char*)pcm.data(), (int)(pcm.size()*sizeof(int16_t)))){
            const char* j = vosk_recognizer_result(rec_);
            Straf::StrafLog(spdlog::level::debug, std::string("Vosk: Got final result: ") + (j ? j : "(null)"));
            StrafLog(spdlog::level::trace, std::format("Vosk: Got final result: {}", j ? j : "(null)"));
            ParseAndEmit(j);
        }
        // Skip partial results - we only want final complete phrases
    }

    void ParseAndEmit(const char* json){
        if (!json || !cb_) return;
        
        // Log the raw JSON for debugging
        Straf::StrafLog(spdlog::level::debug, std::string("Vosk: Parsing JSON: ") + (json ? json : "(null)"));
    StrafLog(spdlog::level::trace, std::format("Vosk: Parsing JSON: {}", json));
        
        std::string phrase;
        
        // Parse final result: {"text" : "..."}
        const char* textField = strstr(json, "\"text\" : \"");
        if (textField) {
            textField += 10; // Skip past "text" : "
            const char* endQuote = strchr(textField, '"');
            if (endQuote) {
                phrase = std::string(textField, endQuote);
                Straf::StrafLog(spdlog::level::debug, "Vosk: Extracted final result: '" + phrase + "'");
                StrafLog(spdlog::level::trace, std::format("Vosk: Extracted final result: '{}'", phrase.c_str()));
            }
        }
        
        // Skip empty results
        if (phrase.empty()) {
            Straf::StrafLog(spdlog::level::debug, "Vosk: No text content found or empty result");
            StrafLog(spdlog::level::trace, "Vosk: No text content found or empty result");
            return;
        }
        
        // Send the entire phrase to the callback for detector analysis
        Straf::StrafLog(spdlog::level::debug, "Vosk: Emitting phrase: '" + phrase + "' (confidence: 0.8)");
    StrafLog(spdlog::level::trace, std::format("Vosk: Emitting phrase: '{}' (confidence: 0.8)", phrase.c_str()));
        cb_(phrase, 0.8f);
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
