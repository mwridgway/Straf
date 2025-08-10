#include "Straf/STT.h"
#include "Straf/Logging.h"
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <unordered_set>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace Straf {

static std::string ToLower(std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); }); return s; }

class TranscriberSapi : public ITranscriber, public ISpNotifyCallback {
public:
    TranscriberSapi() = default;
    ~TranscriberSapi() override { Stop(); }

    bool Initialize(const std::vector<std::string>& vocab) override {
        vocab_.clear();
        for (auto& w : vocab) vocab_.insert(ToLower(w));
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
        Shutdown();
    }

    // ISpNotifyCallback
    STDMETHODIMP NotifyCallback(WPARAM, LPARAM) override {
        if (!recog_ || !cb_) return S_OK;
        CSpEvent evt;
        while (evt.GetFrom(static_cast<ISpEventSource*>(recog_.Get())) == S_OK){
            if (evt.eEventId == SPEI_RECOGNITION){
                ISpRecoResult* pRes = evt.RecoResult();
                CSpDynamicString dstr;
                if (SUCCEEDED(pRes->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &dstr, nullptr)) && dstr){
                    std::string phrase;
                    int len = WideCharToMultiByte(CP_UTF8, 0, dstr, -1, nullptr, 0, nullptr, nullptr);
                    phrase.resize(len ? len - 1 : 0);
                    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, dstr, -1, phrase.data(), len, nullptr, nullptr);
                    LogInfo("SAPI phrase: %s", phrase.c_str());
                    // Split into tokens
                    std::string token;
                    for (char c : phrase){
                        if (std::isalpha((unsigned char)c)) token.push_back((char)std::tolower((unsigned char)c));
                        else {
                            if (!token.empty()) Emit(token);
                            token.clear();
                        }
                    }
                    if (!token.empty()) Emit(token);
                }
            }
        }
        return S_OK;
    }

private:
    void Run(){
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(CoCreateInstance(CLSID_SpSharedRecognizer, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&recognizer_)))){
            LogError("SAPI: Create recognizer failed"); return; }
        if (FAILED(recognizer_->CreateRecoContext(&recog_))){ LogError("SAPI: CreateRecoContext failed"); return; }
        if (FAILED(recog_->SetNotifyCallbackInterface(this, 0, 0))){ LogError("SAPI: SetNotifyCallbackInterface failed"); return; }
        ULONGLONG interests = SPFEI(SPEI_RECOGNITION);
        if (FAILED(recog_->SetInterest(interests, interests))){ LogError("SAPI: SetInterest failed"); return; }
        if (FAILED(recognizer_->SetRecoState(SPRST_ACTIVE_ALWAYS))){ LogError("SAPI: SetRecoState failed"); return; }
        if (FAILED(recog_->SetAudioOptions(SPAO_RETAIN_AUDIO, nullptr, nullptr))){ /* optional */ }

        // Use dictation
        if (FAILED(recog_->CreateGrammar(1, &grammar_))){ LogError("SAPI: CreateGrammar failed"); return; }
        if (FAILED(grammar_->LoadDictation(nullptr, SPLO_STATIC))){ LogError("SAPI: LoadDictation failed"); return; }
        if (FAILED(grammar_->SetDictationState(SPRS_ACTIVE))){ LogError("SAPI: Dictation active failed"); return; }

        // Loop until stopped; notifications drive recognition callback
        while (running_) { Sleep(50); }
        CoUninitialize();
    }

    void Shutdown(){
        grammar_.Reset();
        recog_.Reset();
        recognizer_.Reset();
    }

    void Emit(const std::string& tok){
        if (!cb_) return;
        if (!vocab_.empty()){
            if (vocab_.find(tok) == vocab_.end()) return;
        }
        cb_(tok, 0.9f);
    }

    std::unordered_set<std::string> vocab_;
    TokenCallback cb_{};
    std::atomic<bool> running_{false};
    std::thread worker_;
    Microsoft::WRL::ComPtr<ISpRecognizer> recognizer_;
    Microsoft::WRL::ComPtr<ISpRecoContext> recog_;
    Microsoft::WRL::ComPtr<ISpRecoGrammar> grammar_;
};

std::unique_ptr<ITranscriber> CreateTranscriberSapi(){ return std::make_unique<TranscriberSapi>(); }

}
