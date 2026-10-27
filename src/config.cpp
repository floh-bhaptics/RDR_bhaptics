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

// An RDRVR 0/1 setting from one of its ini files: -1 when the file or the key is absent
int rdrvr_flag(const std::wstring& file, const wchar_t* section, const wchar_t* key) {
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return -1;
    std::wstring s = read_str(file, section, key, L"");
    if (s.empty()) return -1;
    return s == L"1" ? 1 : 0;
}

// RDRVR's setting: its menu's choice (%LOCALAPPDATA%\RDRVR\RDRVR.user.ini) first, then its RDRVR.ini in the game
// folder. -1 when neither has it (no RDRVR).
int rdrvr_setting(const std::wstring& game_dir, const wchar_t* section, const wchar_t* key, std::string* source) {
    int v = -1;
    wchar_t* local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        v = rdrvr_flag(std::wstring(local) + L"\\RDRVR\\RDRVR.user.ini", section, key);
        if (v >= 0) *source = "RDRVR.user.ini";
        CoTaskMemFree(local);
    }
    if (v < 0) {
        v = rdrvr_flag(game_dir + L"\\RDRVR.ini", section, key);
        if (v >= 0) *source = "RDRVR.ini";
    }
    return v;
}

}  // namespace

Config load_config(const std::wstring& dir, const std::wstring& game_dir) {
    Config c;
    const std::wstring ini = dir + L"\\RDR_bhaptics.ini";

    c.enabled = read_bool(ini, L"General", L"Enabled", c.enabled);
    c.debug_log = read_bool(ini, L"General", L"DebugLog", c.debug_log);

    // the built-in workspace unless the ini sets another (an empty key keeps the built-in one)
    const std::string app_id = narrow(read_str(ini, L"bHaptics", L"AppId", L""));
    const std::string api_key = narrow(read_str(ini, L"bHaptics", L"ApiKey", L""));
    if (!app_id.empty()) c.app_id = app_id;
    if (!api_key.empty()) c.api_key = api_key;

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
        c.left_handed = rdrvr_setting(game_dir, L"Controls", L"LeftHanded", &c.hand_source) == 1;
    }

    // two-handed long guns: our ini wins, then RDRVR's [Reload] TwoHanded; off without RDRVR (flat play)
    std::wstring two = read_str(ini, L"Hands", L"TwoHanded", L"Auto");
    if (_wcsicmp(two.c_str(), L"On") == 0 || two == L"1") {
        c.two_handed = true;
        c.two_handed_source = "ini";
    } else if (_wcsicmp(two.c_str(), L"Off") == 0 || two == L"0") {
        c.two_handed = false;
        c.two_handed_source = "ini";
    } else {
        c.two_handed = rdrvr_setting(game_dir, L"Reload", L"TwoHanded", &c.two_handed_source) == 1;
    }
    return c;
}

}  // namespace rdrbh
