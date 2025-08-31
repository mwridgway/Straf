#include "Straf/Config.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace Straf {

static std::string ReadAll(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f.is_open()) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::optional<AppConfig> LoadConfig(const std::string& path) {
    std::string text = ReadAll(path);
    if (text.empty()) return std::nullopt;

    nlohmann::json j;
    try { j = nlohmann::json::parse(text); } catch (...) { return std::nullopt; }
    AppConfig cfg;
    if (auto it = j.find("words"); it != j.end() && it->is_array()) {
        for (const auto& w : *it) if (w.is_string()) cfg.words.push_back(w.get<std::string>());
    }
    if (auto it = j.find("penalty"); it != j.end() && it->is_object()) {
        const auto& p = *it;
        if (p.contains("durationSeconds")) cfg.penalty.durationSeconds = p.value("durationSeconds", cfg.penalty.durationSeconds);
        if (p.contains("cooldownSeconds")) cfg.penalty.cooldownSeconds = p.value("cooldownSeconds", cfg.penalty.cooldownSeconds);
        if (p.contains("queueLimit")) cfg.penalty.queueLimit = p.value("queueLimit", cfg.penalty.queueLimit);
    }
    if (auto it = j.find("audio"); it != j.end() && it->is_object()) {
        const auto& a = *it;
        if (a.contains("sampleRate")) cfg.audio.sampleRate = a.value("sampleRate", cfg.audio.sampleRate);
        if (a.contains("channels")) cfg.audio.channels = a.value("channels", cfg.audio.channels);
    }

    return cfg;
}

}
