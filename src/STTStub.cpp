#include "Straf/STT.h"
#include <thread>
#include <atomic>
#include <chrono>

namespace Straf {

class TranscriberStub : public ITranscriber {
public:
    bool Initialize(const std::vector<std::string>&, const std::shared_ptr<spdlog::logger>& logger) override { 
        logger_ = logger;
        if (logger_) logger_->debug("TranscriberStub::Initialize");
        return true; 
    }
    void Start(TokenCallback onToken) override {
        if (logger_) logger_->debug("TranscriberStub::Start");
        stop_ = false;
        worker_ = std::thread([this, onToken]{
            while(!stop_){
                std::this_thread::sleep_for(std::chrono::seconds(1));
                // no actual tokens; this is a stub
            }
        });
    }
    void Stop() override {
        if (logger_) logger_->debug("TranscriberStub::Stop");
        stop_ = true; if (worker_.joinable()) worker_.join();
    }
private:
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::shared_ptr<spdlog::logger> logger_;
};

std::unique_ptr<ITranscriber> CreateTranscriberStub(){
    return std::make_unique<TranscriberStub>();
}

}
