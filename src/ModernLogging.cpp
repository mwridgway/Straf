// Thin wrapper around spdlog for Straf logging
#include "Straf/ModernLogging.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>
#include <memory>
#include <mutex>

namespace Straf {

namespace {
    std::shared_ptr<spdlog::logger> g_logger;
    std::mutex g_loggerMutex;
    bool g_initialized = false;

    void InitializeSpdlog(const std::string& logFile, spdlog::level::level_enum level) {
        if (g_initialized) return;
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#ifdef _WIN32
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif
        if (!logFile.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true));
        }
        g_logger = std::make_shared<spdlog::logger>("straf", sinks.begin(), sinks.end());
        g_logger->set_level(level);
        spdlog::set_default_logger(g_logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::flush_on(spdlog::level::err);
        g_initialized = true;
    }
}

void StrafLogInit(const std::string& logFile, spdlog::level::level_enum level) {
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    InitializeSpdlog(logFile, level);
}

void StrafLogSetLevel(spdlog::level::level_enum level) {
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    if (g_logger) g_logger->set_level(level);
}

void StrafLogShutdown() {
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    g_logger.reset();
    spdlog::shutdown();
    g_initialized = false;
}

void StrafLog(spdlog::level::level_enum level, const std::string& msg) {
    if (g_logger) g_logger->log(level, msg);
}

} // namespace Straf
