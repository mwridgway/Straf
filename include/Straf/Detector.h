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

/**
 * @brief Alias for a callback function that handles detection results.
 *
 * This type defines a callback function that takes a constant reference to a DetectionResult
 * and returns void. It is typically used to notify clients when a detection event occurs.
 */
using DetectionCallback = std::function<void(const DetectionResult&)>;

/**
 * @brief Interface for a detection engine.
 *
 * This interface defines the contract for a detection engine, requiring implementations to provide 
 * methods for initialization with a vocabulary, starting detection with a callback, and stopping 
 * the detection process. It ensures extensibility by using pure virtual functions and includes a 
 * virtual destructor for proper cleanup.
 */
class IDetector {
public:
    virtual ~IDetector() = default;
    virtual bool Initialize(const std::vector<std::string>& vocabulary) = 0;
    virtual void Start(DetectionCallback onDetect) = 0;
    virtual void Stop() = 0;
};

/**
 * @brief Interface for text-based detection that can analyze recognized speech.
 */
class ITextDetector : public IDetector {
public:
    virtual void AnalyzeText(const std::string& recognizedText, float confidence = 1.0f) = 0;
};

std::unique_ptr<IDetector> CreateDetectorStub();
std::unique_ptr<ITextDetector> CreateTextAnalysisDetector();

}
