#include "log.h"

#include "rh.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace rdrbh::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
bool g_debug = false;

void write(const char* level, LogType console, bool to_console, const char* fmt, va_list ap) {
    char msg[1024];
    std::vsnprintf(msg, sizeof(msg), fmt, ap);

    SYSTEMTIME t;
    GetLocalTime(&t);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) {
            std::fprintf(g_file, "%02d:%02d:%02d.%03d [%s] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, level, msg);
            std::fflush(g_file);
        }
    }
    if (to_console) Print(console, "[RDR_bhaptics] %s", msg);
}

}  // namespace

void open(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) return;
    _wfopen_s(&g_file, path.c_str(), L"w");
}

void set_debug(bool on) { g_debug = on; }
bool debug_on() { return g_debug; }

void info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    write("info", Log_Info, false, fmt, ap);
    va_end(ap);
}

void warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    write("warn", Log_Warning, true, fmt, ap);
    va_end(ap);
}

void error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    write("error", Log_Error, true, fmt, ap);
    va_end(ap);
}

void debug(const char* fmt, ...) {
    if (!g_debug) return;
    va_list ap;
    va_start(ap, fmt);
    write("debug", Log_Info, false, fmt, ap);
    va_end(ap);
}

}  // namespace rdrbh::log
