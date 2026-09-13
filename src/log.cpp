#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>

namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

} // namespace

void mwlog::Open(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    // mode "w" truncates; the log is per-session.
    //
    // Deliberately _wfsopen with _SH_DENYNO rather than _wfopen_s: the default
    // sharing mode locks the file for the whole lifetime of the injected DLL,
    // so you cannot read the log while the game is running - which is exactly
    // when you need it. This allows another process to open it for reading.
    g_file = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
}

void mwlog::Close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void mwlog::Line(const char* fmt, ...) {
    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_file) {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        std::fprintf(g_file, "[%02u:%02u:%02u] %s\n", st.wHour, st.wMinute, st.wSecond,
                     buffer);
        std::fflush(g_file);
    }

    ::OutputDebugStringA(buffer);
    ::OutputDebugStringA("\n");
}
