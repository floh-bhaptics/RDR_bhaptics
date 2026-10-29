#pragma once
// RDR_bhaptics\RDR_bhaptics.ini, next to the .red. Read once at load.

#include <cstdint>
#include <string>

namespace rdrbh {

struct Config {
    // [General]
    bool enabled = true;
    bool debug_log = false;

    // [bHaptics]
    // The mod's workspace in the bHaptics developer portal. Built in; [bHaptics] AppId / ApiKey in the ini override
    // them only when set (for testing another workspace).
    std::string app_id = "6ae0a3f009cd02d96fb42fae";
    std::string api_key = "4WvQBCQIyg8V23Z62vSV";

    // [Hands]
    // Which arm a shot / reload plays on. Auto: RDRVR's [Controls] LeftHanded (its RDRVR.ini in the game folder, then
    // %LOCALAPPDATA%\RDRVR\RDRVR.user.ini, where its menu saves), else right-handed.
    bool left_handed = false;
    std::string hand_source = "default";  // for the log: "ini", "RDRVR.ini", "RDRVR.user.ini", "default"

    // Long guns (rifles, shotguns) play their "_TwoHanded_" recoil. Auto: RDRVR's [Reload] TwoHanded (its foregrip
    // hold) is on. Note this is whether the feature is on, not whether the front hand grips the gun at that shot:
    // RDRVR keeps that inside its own core, out of reach of natives.
    bool two_handed = false;
    std::string two_handed_source = "default";

    // [Intensity] multipliers, 0 turns the effect off
    float recoil = 1.0f;
    float impact = 1.0f;
    float heartbeat = 1.0f;
    float deadeye = 1.0f;
    float horse = 1.0f;
    float environment = 1.0f;  // fire, water
    float movement = 1.0f;     // 1.1.0: draw / holster, jump / land, mount / dismount, whistle
    float rain = 1.0f;         // 1.1.0: the rain splashes

    // [Impact]
    float hit_angle_offset = 0.0f;  // degrees added to the computed hit angle (calibration)
    bool hit_angle_invert = false;  // mirror left/right (calibration)
    float min_damage = 0.5f;        // health drops smaller than this are ignored
    // 1.1.0: a hit's weapon and attacker count only when HEALTH::GET_LAST_HIT_TIME moved (a fresh attack); off: always
    bool check_hit_time = true;

    // [Health]
    float low_health_ratio = 0.25f;  // heartbeat below this share of max health
    float heal_ratio = 0.15f;        // a rise of at least this share of max health in one tick plays "Heal"

    // [Horse]
    float trot_speed = 3.0f;    // m/s
    float gallop_speed = 7.5f;  // m/s

    // [Movement] 1.1.0
    uint64_t min_air_ms = 250;  // a landing plays after at least this long in the air

    // [Rain] 1.1.0
    int rain_min_ms = 100;          // the random pause between two splashes
    int rain_max_ms = 500;
    bool rain_outdoors_only = true; // ENTITY::IS_ACTOR_OUTDOORS
};

// Loads the ini from `dir` (the plugin folder) and resolves the hand. Missing keys keep their defaults.
Config load_config(const std::wstring& dir, const std::wstring& game_dir);

}  // namespace rdrbh
