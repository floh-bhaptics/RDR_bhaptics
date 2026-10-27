#include "haptics.h"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <bhaptics/bhaptics.h>

#include "log.h"

#define BHAPTICS_STR2(x) #x
#define BHAPTICS_STR(x) BHAPTICS_STR2(x)

namespace rdrbh::haptics {
namespace {

// The calls we need take their types from the SDK's own declarations, so a signature change in a new header fails to
// compile here. Two exports are newer than some SDK2 releases (bHapticsGetVersion, bHapticsShutdown: 2.7 / 2.8), so
// they get types of their own and are optional at runtime.
using GetVersionFn = const char*(__cdecl*)(void);
using ShutdownFn = void(__cdecl*)(void);

struct Api {
    GetVersionFn get_version = nullptr;  // optional
    decltype(&::registryAndInit) registry_and_init = nullptr;
    decltype(&::wsIsConnected) ws_is_connected = nullptr;
    decltype(&::playParam) play_param = nullptr;
    decltype(&::stopByEventId) stop_by_event_id = nullptr;
    decltype(&::stopAll) stop_all = nullptr;
    ShutdownFn shutdown = nullptr;  // optional
};

enum class Op { Play, Stop, StopAll, Quit };

struct Command {
    Op op;
    std::string event;
    float intensity, duration, angle_x, offset_y;
};

HMODULE g_dll = nullptr;
Api g_api;
std::atomic<bool> g_ready{false};

std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<Command> g_queue;
std::thread g_thread;

constexpr size_t kMaxQueue = 64;  // the player gone or slow: drop the oldest rather than grow

template <typename F>
bool resolve(F& fn, const char* name) {
    fn = reinterpret_cast<F>(GetProcAddress(g_dll, name));
    if (!fn) log::error("bhaptics_library.dll has no export %s (an older SDK?)", name);
    return fn != nullptr;
}

void push(Command c) {
    if (!g_ready.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_queue.size() >= kMaxQueue) g_queue.pop_front();
        g_queue.push_back(std::move(c));
    }
    g_cv.notify_one();
}

void worker() {
    for (;;) {
        Command c;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, [] { return !g_queue.empty(); });
            c = std::move(g_queue.front());
            g_queue.pop_front();
        }
        switch (c.op) {
            case Op::Play: {
                if (!g_api.ws_is_connected()) break;  // nothing to play to; don't let the SDK queue it up
                const int id = g_api.play_param(c.event.c_str(), -1, c.intensity, c.duration, c.angle_x, c.offset_y);
                if (id < 0) log::debug("play %s failed (not in the workspace?)", c.event.c_str());
                break;
            }
            case Op::Stop:
                g_api.stop_by_event_id(c.event.c_str());
                break;
            case Op::StopAll:
                g_api.stop_all();
                break;
            case Op::Quit:
                return;
        }
    }
}

}  // namespace

bool start(const std::wstring& dll_path, const std::string& app_id, const std::string& api_key) {
    if (g_ready) return true;
    if (app_id.empty() || api_key.empty()) {
        log::error("no AppId / ApiKey in RDR_bhaptics.ini [bHaptics]");
        return false;
    }
    // LOAD_WITH_ALTERED_SEARCH_PATH: the DLL's own dependencies are looked up next to it, not in the game folder
    g_dll = LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_dll) {
        log::error("could not load %ls (error %lu)", dll_path.c_str(), GetLastError());
        return false;
    }
    bool ok = true;  // resolve all, so the log names every missing export
    ok &= resolve(g_api.registry_and_init, "registryAndInit");
    ok &= resolve(g_api.ws_is_connected, "wsIsConnected");
    ok &= resolve(g_api.play_param, "playParam");
    ok &= resolve(g_api.stop_by_event_id, "stopByEventId");
    ok &= resolve(g_api.stop_all, "stopAll");
    if (!ok) {
        FreeLibrary(g_dll);
        g_dll = nullptr;
        return false;
    }
    g_api.get_version = reinterpret_cast<GetVersionFn>(GetProcAddress(g_dll, "bHapticsGetVersion"));
    g_api.shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(g_dll, "bHapticsShutdown"));
#ifdef BHAPTICS_VERSION_MAJOR
    const char* header = BHAPTICS_STR(BHAPTICS_VERSION_MAJOR) "." BHAPTICS_STR(BHAPTICS_VERSION_MINOR) "." BHAPTICS_STR(BHAPTICS_VERSION_PATCH);
#else
    const char* header = "older than 2.7";
#endif
    log::info("bHaptics SDK: DLL %s, header %s", g_api.get_version ? g_api.get_version() : "older than 2.7", header);
    if (!g_api.shutdown) log::info("bhaptics_library.dll has no bHapticsShutdown (older SDK): fine while the game exits with it");

    // returns at once; connects (and reconnects when the player starts later) in the background
    if (!g_api.registry_and_init(api_key.c_str(), app_id.c_str(), "")) {
        log::error("registryAndInit could not start");
        return false;
    }
    g_ready = true;
    g_thread = std::thread(worker);
    return true;
}

void stop_and_unload() {
    if (!g_ready.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.clear();
        g_queue.push_back({Op::Quit, {}, 0, 0, 0, 0});
    }
    g_cv.notify_one();
    if (g_thread.joinable()) g_thread.join();
    g_api.stop_all();
    if (g_api.shutdown) {
        g_api.shutdown();  // joins the SDK's worker so FreeLibrary is safe
        FreeLibrary(g_dll);
    }  // else: an older SDK has no way to stop its thread; leave the DLL loaded rather than crash on unload
    g_dll = nullptr;
}

bool connected() { return g_ready && g_api.ws_is_connected(); }

// The portal and the SDK only know lowercase event ids; the code keeps readable CamelCase names ("RecoilPistol_R"),
// lowercased here on their way to the SDK ("recoilpistol_r").
std::string event_id(const char* name) {
    std::string id(name ? name : "");
    for (char& c : id) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return id;
}

void play(const char* event, float intensity, float duration, float angle_x, float offset_y) {
    if (intensity <= 0.0f) return;
    push({Op::Play, event_id(event), intensity, duration, angle_x, offset_y});
}

void stop(const char* event) { push({Op::Stop, event_id(event), 0, 0, 0, 0}); }

void stop_all() { push({Op::StopAll, {}, 0, 0, 0, 0}); }

}  // namespace rdrbh::haptics
