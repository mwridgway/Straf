#include "Straf/STT.h"
#include "Straf/Logging.h"

namespace Straf {

class TranscriberVoskStub : public ITranscriber {
public:
    bool Initialize(const std::vector<std::string>&) override { return true; }
    void Start(TokenCallback) override {
        LogError("Vosk backend not enabled at build time (STRAF_ENABLE_VOSK=OFF)");
    }
    void Stop() override {}
};

std::unique_ptr<ITranscriber> CreateTranscriberVosk(){ return std::make_unique<TranscriberVoskStub>(); }

}
