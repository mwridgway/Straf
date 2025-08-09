#include "Straf/Config.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#if __has_include(<nlohmann/json.hpp>)
    #include <nlohmann/json.hpp>
    #define STRAF_HAS_NLOHMANN 1
#else
    #include "Straf/MiniJson.h"
#endif

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

#ifdef STRAF_HAS_NLOHMANN
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
    if (auto it = j.find("logging"); it != j.end() && it->is_object()) {
        cfg.logLevel = it->value("level", cfg.logLevel);
    } else if (j.contains("level")) {
        cfg.logLevel = j.value("level", cfg.logLevel);
    }
    return cfg;
#else
    auto rootOpt = MiniJson::Parse(text);
    if (!rootOpt || !rootOpt->is_object()) return std::nullopt;
    const auto& root = rootOpt->as_object();
    AppConfig cfg;
    // words
    if (const auto* v = MiniJson::Find(root, "words"); v && v->is_array()){
        for (const auto& el : v->as_array()) if (el.is_string()) cfg.words.push_back(el.as_string());
    }
    // penalty
    if (const auto* v = MiniJson::Find(root, "penalty"); v && v->is_object()){
        const auto& p = v->as_object();
        if (const auto* d = MiniJson::Find(p, "durationSeconds"); d && d->is_number()) cfg.penalty.durationSeconds = static_cast<int>(d->as_number());
        if (const auto* c = MiniJson::Find(p, "cooldownSeconds"); c && c->is_number()) cfg.penalty.cooldownSeconds = static_cast<int>(c->as_number());
        if (const auto* q = MiniJson::Find(p, "queueLimit"); q && q->is_number()) cfg.penalty.queueLimit = static_cast<int>(q->as_number());
    }
    // audio
    if (const auto* v = MiniJson::Find(root, "audio"); v && v->is_object()){
        const auto& a = v->as_object();
        if (const auto* sr = MiniJson::Find(a, "sampleRate"); sr && sr->is_number()) cfg.audio.sampleRate = static_cast<int>(sr->as_number());
        if (const auto* ch = MiniJson::Find(a, "channels"); ch && ch->is_number()) cfg.audio.channels = static_cast<int>(ch->as_number());
    }
    // logging
    if (const auto* v = MiniJson::Find(root, "logging"); v && v->is_object()){
        const auto& l = v->as_object();
        if (const auto* lev = MiniJson::Find(l, "level"); lev && lev->is_string()) cfg.logLevel = lev->as_string();
    } else if (const auto* lev = MiniJson::Find(root, "level"); lev && lev->is_string()) {
        cfg.logLevel = lev->as_string();
    }
    return cfg;
#endif
}

}
