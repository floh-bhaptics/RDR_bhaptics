#include "config.h"

#include <windows.h>
#include <shlobj.h>

#include <cwchar>
#include <cstdlib>

namespace rdrbh {
namespace {

std::wstring read_str(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512];
    GetPrivateProfileStringW(section, key, def, buf, 512, file.c_str());
    // strip an inline "; comment"
    if (wchar_t* c = std::wcschr(buf, L';')) *c = 0;
    std::wstring s(buf);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    return s;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

float read_float(const std::wstring& file, const wchar_t* section, const wchar_t* key, float def) {
    std::wstring s = read_str(file, section, key, L"");
    if (s.empty()) return def;
    wchar_t* end = nullptr;
    float v = std::wcstof(s.c_str(), &end);
    return end != s.c_str() ? v : def;
}

bool read_bool(const std::wstring& file, const wchar_t* section, const wchar_t* key, bool def) {
    std::wstring s = read_str(file, section, key, L"");
    if (s.empty()) return def;
    return s == L"1" || _wcsicmp(s.c_str(), L"true") == 0 || _wcsicmp(s.c_str(), L"on") == 0;
}

// RDRVR's [Controls] LeftHanded: -1 when the key is absent from that file
int rdrvr_left_handed(const std::wstring& file) {
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return -1;
    std::wstring s = read_str(file, L"Controls", L"LeftHanded", L"");
    if (s.empty()) return -1;
    return s == L"1" ? 1 : 0;
}

}  // namespace

Config load_config(const std::wstring& dir, const std::wstring& game_dir) {
    Config c;
    const std::wstring ini = dir + L"\\RDR_bhaptics.ini";

    c.enabled = read_bool(ini, L"General", L"Enabled", c.enabled);
    c.debug_log = read_bool(ini, L"General", L"DebugLog", c.debug_log);

    c.app_id = narrow(read_str(ini, L"bHaptics", L"AppId", L""));
    c.api_key = narrow(read_str(ini, L"bHaptics", L"ApiKey", L""));

    c.recoil = read_float(ini, L"Intensity", L"Recoil", c.recoil);
    c.impact = read_float(ini, L"Intensity", L"Impact", c.impact);
    c.heartbeat = read_float(ini, L"Intensity", L"Heartbeat", c.heartbeat);
    c.deadeye = read_float(ini, L"Intensity", L"Deadeye", c.deadeye);
    c.horse = read_float(ini, L"Intensity", L"Horse", c.horse);
    c.environment = read_float(ini, L"Intensity", L"Environment", c.environment);

    c.hit_angle_offset = read_float(ini, L"Impact", L"AngleOffset", c.hit_angle_offset);
    c.hit_angle_invert = read_bool(ini, L"Impact", L"AngleInvert", c.hit_angle_invert);
    c.min_damage = read_float(ini, L"Impact", L"MinDamage", c.min_damage);

    c.low_health_ratio = read_float(ini, L"Health", L"LowHealthRatio", c.low_health_ratio);
    c.heal_ratio = read_float(ini, L"Health", L"HealRatio", c.heal_ratio);

    c.trot_speed = read_float(ini, L"Horse", L"TrotSpeed", c.trot_speed);
    c.gallop_speed = read_float(ini, L"Horse", L"GallopSpeed", c.gallop_speed);

    // the hand: our ini wins, then RDRVR's user ini (its menu), then its shipped ini
    std::wstring hand = read_str(ini, L"Hands", L"Hand", L"Auto");
    if (_wcsicmp(hand.c_str(), L"Left") == 0) {
        c.left_handed = true;
        c.hand_source = "ini";
    } else if (_wcsicmp(hand.c_str(), L"Right") == 0) {
        c.left_handed = false;
        c.hand_source = "ini";
    } else {
        wchar_t* local = nullptr;
        int lh = -1;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            lh = rdrvr_left_handed(std::wstring(local) + L"\\RDRVR\\RDRVR.user.ini");
            if (lh >= 0) c.hand_source = "RDRVR.user.ini";
            CoTaskMemFree(local);
        }
        if (lh < 0) {
            lh = rdrvr_left_handed(game_dir + L"\\RDRVR.ini");
            if (lh >= 0) c.hand_source = "RDRVR.ini";
        }
        c.left_handed = lh == 1;
    }
    return c;
}

}  // namespace rdrbh
