#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <format>
#include <initializer_list>
#include <utility>

namespace Straf {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5
};

struct LogField {
    std::string_view key;
    std::string value;
    
    LogField(std::string_view k, std::string_view v) : key(k), value(v) {}
    LogField(std::string_view k, const std::string& v) : key(k), value(v) {}
    LogField(std::string_view k, int v) : key(k), value(std::to_string(v)) {}
    LogField(std::string_view k, float v) : key(k), value(std::format("{:.2f}", v)) {}
    LogField(std::string_view k, double v) : key(k), value(std::format("{:.2f}", v)) {}
    LogField(std::string_view k, bool v) : key(k), value(v ? "true" : "false") {}
};

// Modern logger interface following SOLID principles
class ILogger {
public:
    virtual ~ILogger() = default;
    
    // Core logging method with structured fields
    virtual void Log(LogLevel level, std::string_view message, 
                     std::initializer_list<LogField> fields = {}) = 0;
    
    // Check if a log level is enabled (for performance)
    virtual bool ShouldLog(LogLevel level) const = 0;
    
    // Create a child logger with additional context
    virtual std::shared_ptr<ILogger> CreateChild(std::string_view component) = 0;
    
    // Templated convenience methods with std::format
    template<typename... Args>
    void Trace(std::format_string<Args...> fmt, Args&&... args) {
        if (ShouldLog(LogLevel::Trace)) {
            Log(LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...));
        }
    }
    
    template<typename... Args>
    void Debug(std::format_string<Args...> fmt, Args&&... args) {
        if (ShouldLog(LogLevel::Debug)) {
            Log(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
        }
    }
    
    template<typename... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args) {
        if (ShouldLog(LogLevel::Info)) {
            Log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
        }
    }
    
    template<typename... Args>
    void Warn(std::format_string<Args...> fmt, Args&&... args) {
        if (ShouldLog(LogLevel::Warn)) {
            Log(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
        }
    }
    
    template<typename... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args) {
        if (ShouldLog(LogLevel::Error)) {
            Log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
        }
    }
    
    template<typename... Args>
    void Critical(std::format_string<Args...> fmt, Args&&... args) {
        if (ShouldLog(LogLevel::Critical)) {
            Log(LogLevel::Critical, std::format(fmt, std::forward<Args>(args)...));
        }
    }
    
    // Structured logging with fields
    template<typename... Args>
    void InfoWith(std::format_string<Args...> fmt, 
                  std::initializer_list<LogField> fields, 
                  Args&&... args) {
        if (ShouldLog(LogLevel::Info)) {
            Log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...), fields);
        }
    }
    
    template<typename... Args>
    void ErrorWith(std::format_string<Args...> fmt, 
                   std::initializer_list<LogField> fields, 
                   Args&&... args) {
        if (ShouldLog(LogLevel::Error)) {
            Log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...), fields);
        }
    }
};

// Factory for creating loggers
class LoggerFactory {
public:
    // Create the main application logger
    static std::shared_ptr<ILogger> CreateLogger(const std::string& name, 
                                                 LogLevel level = LogLevel::Info,
                                                 const std::string& logFile = "");
    
    // Configure global logging settings
    static void SetGlobalLevel(LogLevel level);
    static void SetLogPattern(const std::string& pattern);
    
    // Shutdown logging system cleanly
    static void Shutdown();
};

// RAII scope logger for automatic context
class ScopedLogger {
public:
    ScopedLogger(std::shared_ptr<ILogger> logger, std::string_view scope, 
                 std::initializer_list<LogField> fields = {})
        : logger_(logger), scope_(scope) {
        
        if (logger_->ShouldLog(LogLevel::Debug)) {
            std::string fieldsStr;
            for (const auto& field : fields) {
                if (!fieldsStr.empty()) fieldsStr += ", ";
                fieldsStr += std::format("{}={}", field.key, field.value);
            }
            logger_->Debug("Entering {} {}", scope_, 
                          fieldsStr.empty() ? "" : std::format("({})", fieldsStr));
        }
    }
    
    ~ScopedLogger() {
        if (logger_->ShouldLog(LogLevel::Debug)) {
            logger_->Debug("Exiting {}", scope_);
        }
    }
    
private:
    std::shared_ptr<ILogger> logger_;
    std::string scope_;
};

// Convenience macro for scoped logging
#define LOG_SCOPE(logger, scope, ...) \
    ScopedLogger _scope_logger(logger, scope, ##__VA_ARGS__)

} // namespace Straf
