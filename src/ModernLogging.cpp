#include "Straf/ModernLogging.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>
#include <unordered_map>
#include <mutex>

namespace Straf {

namespace {
    // Convert our log level to spdlog level
    spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return spdlog::level::trace;
            case LogLevel::Debug: return spdlog::level::debug;
            case LogLevel::Info: return spdlog::level::info;
            case LogLevel::Warn: return spdlog::level::warn;
            case LogLevel::Error: return spdlog::level::err;
            case LogLevel::Critical: return spdlog::level::critical;
            default: return spdlog::level::info;
        }
    }
    
    LogLevel FromSpdlogLevel(spdlog::level::level_enum level) {
        switch (level) {
            case spdlog::level::trace: return LogLevel::Trace;
            case spdlog::level::debug: return LogLevel::Debug;
            case spdlog::level::info: return LogLevel::Info;
            case spdlog::level::warn: return LogLevel::Warn;
            case spdlog::level::err: return LogLevel::Error;
            case spdlog::level::critical: return LogLevel::Critical;
            default: return LogLevel::Info;
        }
    }
}

// Modern spdlog-based logger implementation
class SpdlogLogger : public ILogger {
public:
    explicit SpdlogLogger(std::shared_ptr<spdlog::logger> logger, std::string component = "")
        : spdlogger_(logger), component_(component) {}
    
    void Log(LogLevel level, std::string_view message, 
             std::initializer_list<LogField> fields = {}) override {
        
        if (!spdlogger_->should_log(ToSpdlogLevel(level))) return;
        
        std::string fullMessage{message};
        
        // Add component prefix if available
        if (!component_.empty()) {
            fullMessage = std::format("[{}] {}", component_, fullMessage);
        }
        
        // Add structured fields if provided
        if (fields.size() > 0) {
            fullMessage += " |";
            for (const auto& field : fields) {
                fullMessage += std::format(" {}={}", field.key, field.value);
            }
        }
        
        spdlogger_->log(ToSpdlogLevel(level), fullMessage);
    }
    
    bool ShouldLog(LogLevel level) const override {
        return spdlogger_->should_log(ToSpdlogLevel(level));
    }
    
    std::shared_ptr<ILogger> CreateChild(std::string_view childComponent) override {
        std::string fullComponent = component_.empty() 
            ? std::string{childComponent}
            : std::format("{}.{}", component_, childComponent);
        
        return std::make_shared<SpdlogLogger>(spdlogger_, fullComponent);
    }
    
private:
    std::shared_ptr<spdlog::logger> spdlogger_;
    std::string component_;
};

// Factory implementation
namespace {
    std::mutex g_loggerMutex;
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
    bool g_initialized = false;
    
    void InitializeSpdlog() {
        if (g_initialized) return;
        
        // Set global pattern for consistent formatting
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        
        // Flush logs immediately on error/critical
        spdlog::flush_on(spdlog::level::err);
        
        g_initialized = true;
    }
}

std::shared_ptr<ILogger> LoggerFactory::CreateLogger(const std::string& name, 
                                                     LogLevel level,
                                                     const std::string& logFile) {
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    
    InitializeSpdlog();
    
    // Check if logger already exists
    auto it = g_loggers.find(name);
    if (it != g_loggers.end()) {
        return std::make_shared<SpdlogLogger>(it->second);
    }
    
    // Create sinks
    std::vector<spdlog::sink_ptr> sinks;
    
    // Console sink (colored output)
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(console_sink);
    
#ifdef _WIN32
    // Visual Studio output window sink (Windows only)
    auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    sinks.push_back(msvc_sink);
#endif
    
    // File sink if specified
    if (!logFile.empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        sinks.push_back(file_sink);
    }
    
    // Create multi-sink logger
    auto spdlogger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    spdlogger->set_level(ToSpdlogLevel(level));
    
    // Register with spdlog
    spdlog::register_logger(spdlogger);
    
    // Cache for reuse
    g_loggers[name] = spdlogger;
    
    return std::make_shared<SpdlogLogger>(spdlogger);
}

void LoggerFactory::SetGlobalLevel(LogLevel level) {
    spdlog::set_level(ToSpdlogLevel(level));
}

void LoggerFactory::SetLogPattern(const std::string& pattern) {
    spdlog::set_pattern(pattern);
}

void LoggerFactory::Shutdown() {
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    g_loggers.clear();
    spdlog::shutdown();
}

} // namespace Straf
