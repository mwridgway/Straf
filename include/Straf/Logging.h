#pragma once
#include <string>

namespace Straf {

void InitLogging(const std::string& level);
void ShutdownLogging();
void LogInfo(const char* fmt, ...);
void LogVerbose(const char* fmt, ...);
void LogError(const char* fmt, ...);

}
