#include "Straf/STT.h"
#include <thread>
#include <atomic>
#include <chrono>

namespace Straf {

class TranscriberStub : public ITranscriber {
public:
    bool Initialize(const std::vector<std::string>&) override { return true; }
    void Start(TokenCallback onToken) override {
        stop_ = false;
        worker_ = std::thread([this, onToken]{
            while(!stop_){
                std::this_thread::sleep_for(std::chrono::seconds(1));
                // no actual tokens; this is a stub
            }
        });
    }
    void Stop() override {
        stop_ = true; if (worker_.joinable()) worker_.join();
    }
private:
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

std::unique_ptr<ITranscriber> CreateTranscriberStub(){
    return std::make_unique<TranscriberStub>();
}

}
