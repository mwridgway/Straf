// ModernLogging.h: Only thin spdlog wrapper declarations remain
#pragma once
#include <string>
#include <spdlog/spdlog.h>

namespace Straf {

// Thin wrapper API for spdlog
void StrafLogInit(const std::string& logFile, spdlog::level::level_enum level);
void StrafLogSetLevel(spdlog::level::level_enum level);
void StrafLogShutdown();
void StrafLog(spdlog::level::level_enum level, const std::string& msg);

} // namespace Straf
