#pragma once
// A plain log file in the plugin's folder (RDR_bhaptics\RDR_bhaptics.log), warnings and errors also to RedHook's console.

#include <string>

namespace rdrbh::log {

void open(const std::wstring& path);
void set_debug(bool on);
bool debug_on();

void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);
void debug(const char* fmt, ...);  // only with [General] DebugLog=1

}  // namespace rdrbh::log
