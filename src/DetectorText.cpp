#include "Straf/Detector.h"

#include "Straf/ModernLogging.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <set>
#include <windows.h>

namespace Straf {

// Forward declaration for stub detector from original file
class DetectorStub : public IDetector {
public:
    bool Initialize(const std::vector<std::string>& vocabulary) override {
        words_ = vocabulary;
        return true;
    }
    void Start(DetectionCallback onDetect) override {
        (void)onDetect; // Suppress unused parameter warning
        // Stub implementation - does nothing for now
    Straf::StrafLog(spdlog::level::info, "Detector: Started stub detector (no actual detection)");
    }
    void Stop() override {
    Straf::StrafLog(spdlog::level::info, "Detector: Stopped stub detector");
    }
private:
    std::vector<std::string> words_;
};

class DetectorNoop : public IDetector {
public:
    bool Initialize(const std::vector<std::string>&) override { return true; }
    void Start(DetectionCallback) override {}
    void Stop() override {}
};

class TextAnalysisDetector : public ITextDetector {
public:
    bool Initialize(const std::vector<std::string>& vocabulary) override {
        vocabulary_.clear();
        
        // Convert vocabulary to lowercase for case-insensitive matching
        for (const auto& word : vocabulary) {
            std::string lowerWord = ToLowerCase(word);
            vocabulary_.insert(lowerWord);
            StrafLog(spdlog::level::trace, "Detector: Added vocabulary word: " + word + " (normalized: " + lowerWord + ")");
        }
        
    Straf::StrafLog(spdlog::level::info, "Detector: Initialized with " + std::to_string(vocabulary_.size()) + " vocabulary words");
        return true;
    }
    
    void Start(DetectionCallback onDetect) override {
        onDetect_ = onDetect;
    Straf::StrafLog(spdlog::level::info, "Detector: Started text analysis detector");
    }
    
    void Stop() override {
        onDetect_ = nullptr;
    Straf::StrafLog(spdlog::level::info, "Detector: Stopped text analysis detector");
    }
    
    // Method to analyze recognized text and detect vocabulary words
    void AnalyzeText(const std::string& recognizedText, float confidence = 1.0f) override {
        if (!onDetect_ || recognizedText.empty()) return;
        
    Straf::StrafLog(spdlog::level::debug, "Detector: Analyzing text: \"" + recognizedText + "\"");
    StrafLog(spdlog::level::trace, "Detector: Analyzing text: \"" + recognizedText + "\"");
        
        // Split text into words and check each against vocabulary
        std::vector<std::string> words = SplitIntoWords(recognizedText);
        
        for (const auto& word : words) {
            std::string lowerWord = ToLowerCase(word);
            if (vocabulary_.count(lowerWord) > 0) {
                Straf::StrafLog(spdlog::level::info, "Detector: Found vocabulary word: \"" + word + "\" in text: \"" + recognizedText + "\"");
                onDetect_(DetectionResult{word, confidence});
            }
        }
    }

private:
    std::set<std::string> vocabulary_;
    DetectionCallback onDetect_;
    
    // Convert string to lowercase
    std::string ToLowerCase(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        return result;
    }
    
    // Split text into individual words, removing punctuation
    std::vector<std::string> SplitIntoWords(const std::string& text) {
        std::vector<std::string> words;
        std::string cleanText = RemovePunctuation(text);
        std::istringstream iss(cleanText);
        std::string word;
        
        while (iss >> word) {
            if (!word.empty()) {
                words.push_back(word);
            }
        }
        
        return words;
    }
    
    // Remove common punctuation from text
    std::string RemovePunctuation(const std::string& text) {
        std::string result;
        for (char c : text) {
            if (std::isalnum(c) || std::isspace(c)) {
                result += c;
            } else {
                result += ' ';  // Replace punctuation with space
            }
        }
        return result;
    }
};

// Factory function for text analysis detector
std::unique_ptr<ITextDetector> CreateTextAnalysisDetector() {
    return std::make_unique<TextAnalysisDetector>();
}

// Extend the existing factory to provide the new detector
std::unique_ptr<IDetector> CreateDetectorStub() { 
    // Check for explicit no-detector mode
    wchar_t dummy[2]{};
    if (GetEnvironmentVariableW(L"STRAF_NO_DETECTOR", dummy, 2) > 0) {
    Straf::StrafLog(spdlog::level::info, "Using no-op detector (STRAF_NO_DETECTOR set)");
        return std::make_unique<DetectorNoop>();
    }
    
    // Check if we should use the old stub detector for testing
    if (GetEnvironmentVariableW(L"STRAF_USE_STUB_DETECTOR", dummy, 2) > 0) {
    Straf::StrafLog(spdlog::level::info, "Using stub detector (STRAF_USE_STUB_DETECTOR set)");
        return std::make_unique<DetectorStub>();
    }
    
    // Default to text analysis detector
    Straf::StrafLog(spdlog::level::info, "Using text analysis detector (default)");
    return CreateTextAnalysisDetector();
}

} // namespace Straf
