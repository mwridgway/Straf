#include "Straf/Logging.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Straf {

static int s_level = 1; // 0=error,1=info
static HANDLE s_logFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION s_logLock;
static bool s_logInitialized = false;

static void EnsureLogFile(){
    if (s_logFile != INVALID_HANDLE_VALUE) return;
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path) == S_OK){
        std::wstring wpath(path);
        CoTaskMemFree(path);
    std::wstring dir = wpath + L"\\Straf\\logs";
    // Recursively create %LocalAppData%\Straf\logs
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring file = dir + L"\\StrafAgent.log";
    s_logFile = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
}

void InitLogging(const std::string& level){
    if (level == "error") s_level = 0; else s_level = 1;
    if (!s_logInitialized) {
        InitializeCriticalSection(&s_logLock);
        s_logInitialized = true;
    }
    EnsureLogFile();
}

void ShutdownLogging() {
    if (s_logInitialized) {
        if (s_logFile != INVALID_HANDLE_VALUE) {
            CloseHandle(s_logFile);
            s_logFile = INVALID_HANDLE_VALUE;
        }
        DeleteCriticalSection(&s_logLock);
        s_logInitialized = false;
    }
}

static std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    struct tm timeinfo;
    if (localtime_s(&timeinfo, &time_t) != 0) {
        return "1970-01-01 00:00:00.000"; // fallback on error
    }
    
    std::ostringstream ss;
    ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

static void vlog(const char* tag, const char* fmt, va_list args){
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len < 0) return; // Encoding error
    if (len >= (int)sizeof(buf)) {
        // Truncated, but continue with what we have
        buf[sizeof(buf) - 1] = '\0';
    }
    
    std::string timestamp = GetTimestamp();
    std::string logLine = timestamp + " " + tag + ": " + buf;
    
    OutputDebugStringA(logLine.c_str());
    OutputDebugStringA("\n");

    if (!s_logInitialized) return;
    
    EnterCriticalSection(&s_logLock);
    EnsureLogFile();
    if (s_logFile != INVALID_HANDLE_VALUE){
        DWORD written = 0;
        std::string line = logLine + "\r\n";
        WriteFile(s_logFile, line.data(), (DWORD)line.size(), &written, nullptr);
        FlushFileBuffers(s_logFile);
    }
    LeaveCriticalSection(&s_logLock);
}

void LogInfo(const char* fmt, ...){
    if (s_level < 1) return;
    va_list args; va_start(args, fmt);
    vlog("[INFO]", fmt, args);
    va_end(args);
}

void LogError(const char* fmt, ...){
    va_list args; va_start(args, fmt);
    vlog("[ERROR]", fmt, args);
    va_end(args);
}

}
