#include "Straf/Config.h"
#include <fstream>
#include <sstream>
#include <regex>

namespace Straf {

static std::string ReadAll(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f.is_open()) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void ParseWords(const std::string& s, std::vector<std::string>& out){
    // Find words array and extract strings
    std::smatch m;
    std::regex wordsArray(R"REGEX("words"\s*:\s*\[(.*?)\])REGEX", std::regex::icase | std::regex::optimize);
    if(std::regex_search(s, m, wordsArray)){
        std::string arr = m[1].str();
        std::regex strRe(R"REGEX("((?:\\"|[^"])*)")REGEX");
        for(auto it = std::sregex_iterator(arr.begin(), arr.end(), strRe); it != std::sregex_iterator(); ++it){
            std::string val = (*it)[1].str();
            // unescape simple escaped quotes
            val = std::regex_replace(val, std::regex("\\\""), "\"");
            out.emplace_back(val);
        }
    }
}

static int ParseInt(const std::string& s, const char* key, int fallback){
    std::regex re(std::string("\"") + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch m; if(std::regex_search(s, m, re)) return std::stoi(m[1].str());
    return fallback;
}

static std::string ParseString(const std::string& s, const char* key, const std::string& fallback){
    std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m; if(std::regex_search(s, m, re)) return m[1].str();
    return fallback;
}

std::optional<AppConfig> LoadConfig(const std::string& path) {
    std::string text = ReadAll(path);  
    if (text.empty()) return std::nullopt;
    AppConfig cfg;
    ParseWords(text, cfg.words);

    cfg.penalty.durationSeconds = ParseInt(text, "durationSeconds", cfg.penalty.durationSeconds);
    cfg.penalty.cooldownSeconds = ParseInt(text, "cooldownSeconds", cfg.penalty.cooldownSeconds);
    cfg.penalty.queueLimit = ParseInt(text, "queueLimit", cfg.penalty.queueLimit);
    cfg.audio.sampleRate = ParseInt(text, "sampleRate", cfg.audio.sampleRate);
    cfg.audio.channels = ParseInt(text, "channels", cfg.audio.channels);
    cfg.logLevel = ParseString(text, "level", cfg.logLevel);
    return cfg;
}

}
