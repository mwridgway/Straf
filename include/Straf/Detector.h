#pragma once
#include <string>
#include <functional>
#include <vector>
#include <memory>

namespace Straf {

struct DetectionResult {
    std::string word;
    float confidence{1.0f};
};

using DetectionCallback = std::function<void(const DetectionResult&)>;

class IDetector {
public:
    virtual ~IDetector() = default;
    virtual bool Initialize(const std::vector<std::string>& vocabulary) = 0;
    virtual void Start(DetectionCallback onDetect) = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IDetector> CreateDetectorStub();

}
