#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <spdlog/spdlog.h>

namespace Straf {

using TokenCallback = std::function<void(const std::string& token, float confidence)>;

class ITranscriber {
public:
    virtual ~ITranscriber() = default;
    // Provide the vocabulary (hints/grammar). Implementations may restrict recognition to this set.
    virtual bool Initialize(const std::vector<std::string>& vocabulary, const std::shared_ptr<spdlog::logger>& logger) = 0;
    virtual void Start(TokenCallback onToken) = 0;
    virtual void Stop() = 0;
};

// Implementations
std::unique_ptr<ITranscriber> CreateTranscriberStub();
std::unique_ptr<ITranscriber> CreateTranscriberSapi();
std::unique_ptr<ITranscriber> CreateTranscriberVosk();

}
