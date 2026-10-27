#pragma once
// bhaptics_library.dll (bHaptics SDK2, tact-cpp2), loaded at runtime from the plugin's own folder.
//
// Every SDK call blocks until its internal worker has handled it, so nothing here calls the SDK on the game's script
// fiber: play / stop only queue a command, and one thread of ours makes the calls. A missing DLL or player just means
// no haptics; the game is never held up.

#include <string>

namespace rdrbh::haptics {

// Loads the DLL from `dll_path` and starts the connection (in the background). False if the DLL or an export is missing.
bool start(const std::wstring& dll_path, const std::string& app_id, const std::string& api_key);
// Joins our thread and calls bHapticsShutdown. Not from DllMain (it joins threads); see main.cpp.
void stop_and_unload();

bool connected();  // the player's websocket is up (a cheap atomic read in the SDK)

// Queue a workspace event. angle_x: degrees around the body (0 front), offset_y: -0.5 .. 0.5 (up / down).
void play(const char* event, float intensity = 1.0f, float duration = 1.0f, float angle_x = 0.0f, float offset_y = 0.0f);
void stop(const char* event);
void stop_all();

}  // namespace rdrbh::haptics
