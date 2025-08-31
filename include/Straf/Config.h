#pragma once
#include <string>
#include <vector>
#include <optional>

namespace Straf {

struct PenaltyConfig {
    int durationSeconds{10};
    int cooldownSeconds{60};
    int queueLimit{5};
};

struct AudioConfig {
    int sampleRate{16000};
    int channels{1};
};

struct AppConfig {
    std::vector<std::string> words;
    PenaltyConfig penalty{};
    AudioConfig audio{};
};

std::optional<AppConfig> LoadConfig(const std::string& path);

}
