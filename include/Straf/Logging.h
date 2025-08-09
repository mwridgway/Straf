#pragma once
#include <string>

namespace Straf {

void InitLogging(const std::string& level);
void LogInfo(const char* fmt, ...);
void LogError(const char* fmt, ...);

}
