// service/src/owb_log.cpp
#include "owb_log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace owb::log {

namespace {

constexpr size_t kLineMax = 1024;

std::mutex  g_mutex;
FILE*       g_file = nullptr;

const char* level_tag(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}

// %ProgramData% resuelto sin depender de shell32 (el servicio corre como LocalSystem).
bool build_log_path(const char* component, wchar_t* path, size_t path_len) {
    wchar_t base[MAX_PATH]{};
    if (!ExpandEnvironmentStringsW(L"%ProgramData%", base, MAX_PATH)) return false;

    wchar_t dir[MAX_PATH]{};
    swprintf_s(dir, MAX_PATH, L"%s\\OpenWinBlue", base);
    CreateDirectoryW(dir, nullptr);
    swprintf_s(dir, MAX_PATH, L"%s\\OpenWinBlue\\logs", base);
    CreateDirectoryW(dir, nullptr);

    swprintf_s(path, path_len, L"%s\\%hs.log", dir, component);
    return true;
}

} // namespace

void init(const char* component) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) return;

    wchar_t path[MAX_PATH]{};
    if (build_log_path(component, path, MAX_PATH)) {
        _wfopen_s(&g_file, path, L"a");
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char header[kLineMax]{};
    sprintf_s(header, kLineMax,
              "===== %s start %04u-%02u-%02u %02u:%02u:%02u pid=%lu =====\n",
              component, st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    if (g_file) { fputs(header, g_file); fflush(g_file); }
    OutputDebugStringA(header);
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

void write(Level level, const char* fmt, ...) {
    char msg[kLineMax]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, kLineMax, fmt, args);
    va_end(args);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char line[kLineMax]{};
    sprintf_s(line, kLineMax, "%02u:%02u:%02u.%03u [%s] [t%05lu] %s\n",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              level_tag(level), GetCurrentThreadId(), msg);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fputs(line, g_file);
        fflush(g_file);
    }
    OutputDebugStringA(line);
    fputs(line, stdout);
}

} // namespace owb::log
