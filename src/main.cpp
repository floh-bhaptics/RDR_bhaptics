// RDR_bhaptics.red: bHaptics for Red Dead Redemption (PC, 2024), as a RedHook plugin.
//
// Layout in the game folder (next to RDR.exe and the other .red plugins):
//   RDR_bhaptics.red
//   RDR_bhaptics\RDR_bhaptics.ini
//   RDR_bhaptics\bhaptics_library.dll
//   RDR_bhaptics\RDR_bhaptics.log        (written)
//
// DllMain only registers the script: everything else (LoadLibrary, threads) happens on the first script tick, outside
// the loader lock.

#include "rh.h"

#include <string>

#include "config.h"
#include "game_state.h"
#include "haptics.h"
#include "log.h"

namespace {

HMODULE g_module = nullptr;

std::wstring dir_of(HMODULE mod) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
    std::wstring s(path, n);
    const size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

// RedHook's scripts are meant to run forever; one that has nothing to do just idles.
[[noreturn]] void idle() {
    for (;;) ScriptWait(1000);
}

void script_main() {
    const std::wstring game_dir = dir_of(nullptr);             // RDR.exe's folder
    const std::wstring plugin_dir = dir_of(g_module) + L"\\RDR_bhaptics";

    rdrbh::log::open(plugin_dir + L"\\RDR_bhaptics.log");
    const rdrbh::Config cfg = rdrbh::load_config(plugin_dir, game_dir);
    rdrbh::log::set_debug(cfg.debug_log);
    rdrbh::log::info("RDR_bhaptics starting (RedHook %d.%d), %s-handed (%s), long guns %s (%s)", RH_GetMajorVersion(),
                     RH_GetMinorVersion(), cfg.left_handed ? "left" : "right", cfg.hand_source.c_str(),
                     cfg.two_handed ? "two-handed" : "one-handed", cfg.two_handed_source.c_str());

    if (!cfg.enabled) {
        rdrbh::log::info("disabled in the ini");
        idle();
    }
    if (!rdrbh::haptics::start(plugin_dir + L"\\bhaptics_library.dll", cfg.app_id, cfg.api_key)) {
        rdrbh::log::error("no haptics this session (see RDR_bhaptics.log)");
        idle();
    }
    rdrbh::game::init(cfg);

    bool was_connected = false;
    uint64_t next_conn_check = 0;
    for (;;) {
        const uint64_t now = GetTickCount64();
        if (now >= next_conn_check) {
            next_conn_check = now + 2000;
            const bool c = rdrbh::haptics::connected();
            if (c != was_connected) rdrbh::log::info(c ? "bHaptics Player connected" : "bHaptics Player not connected");
            was_connected = c;
        }
        rdrbh::game::tick(now);
        ScriptWait(0);  // once per script tick
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = module;
            DisableThreadLibraryCalls(module);
            ScriptRegister(module, script_main);
            break;
        case DLL_PROCESS_DETACH:
            ScriptUnregister(module);
            // reserved != nullptr: the process is exiting and every thread is already gone; nothing to clean up.
            // reserved == nullptr: RedHook unloaded us at runtime. haptics::stop_and_unload() joins threads, which
            // deadlocks under the loader lock, so it is not called here. If RedHook ever starts unloading plugins
            // (it doesn't in 0.8), call it from the script before that instead.
            (void)reserved;
            break;
    }
    return TRUE;
}
