#include "Straf/Logging.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <string>

namespace Straf {

static int s_level = 1; // 0=error,1=info
static HANDLE s_logFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION s_logLock;

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
    InitializeCriticalSection(&s_logLock);
    EnsureLogFile();
}

static void vlog(const char* tag, const char* fmt, va_list args){
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    OutputDebugStringA(tag);
    OutputDebugStringA(": ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

    EnterCriticalSection(&s_logLock);
    EnsureLogFile();
    if (s_logFile != INVALID_HANDLE_VALUE){
        DWORD written = 0;
        std::string line = std::string(tag) + ": " + buf + "\r\n";
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
